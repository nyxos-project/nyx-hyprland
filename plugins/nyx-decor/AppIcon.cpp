#include "AppIcon.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {
    struct SIndex {
        std::mutex                                   lock;
        // Lower-case class, desktop id or its last part -> icon file.
        std::unordered_map<std::string, std::string> icons;
        std::atomic<bool>                            ready{false};
        std::atomic<bool>                            announced{false};
    };
    // Shared with the reader thread, which may outlive an unload.
    std::shared_ptr<SIndex> index;

    std::string lower(std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::vector<fs::path> dataDirs() {
        std::vector<fs::path> out;
        if (const char* home = std::getenv("XDG_DATA_HOME"); home && *home)
            out.emplace_back(home);
        else if (const char* h = std::getenv("HOME"); h && *h)
            out.emplace_back(fs::path(h) / ".local/share");
        std::string dirs = std::getenv("XDG_DATA_DIRS") ? std::getenv("XDG_DATA_DIRS") : "/usr/local/share:/usr/share";
        std::stringstream ss(dirs);
        for (std::string d; std::getline(ss, d, ':');) {
            if (!d.empty())
                out.emplace_back(d);
        }
        return out;
    }

    // An icon name as a file: scalable SVG first, then the larger PNGs, in hicolor, then
    // pixmaps. Absolute paths stand as they are.
    std::string resolve(const std::string& icon, const std::vector<fs::path>& dirs) {
        std::error_code ec;
        if (icon.empty())
            return "";
        if (icon.front() == '/')
            return fs::is_regular_file(icon, ec) ? icon : "";
        static const char* SIZES[] = {"scalable", "64x64", "48x48", "128x128", "256x256", "32x32"};
        for (const auto& d : dirs) {
            for (const char* size : SIZES) {
                for (const char* ext : {".svg", ".png"}) {
                    const auto p = d / "icons/hicolor" / size / "apps" / (icon + ext);
                    if (fs::is_regular_file(p, ec))
                        return p.string();
                }
            }
        }
        for (const auto& d : dirs) {
            for (const char* ext : {".svg", ".png"}) {
                const auto p = d / "pixmaps" / (icon + ext);
                if (fs::is_regular_file(p, ec))
                    return p.string();
            }
        }
        return "";
    }

    void read(const std::shared_ptr<SIndex>& idx, const std::vector<fs::path>& DIRS) {
        std::unordered_map<std::string, std::string> found;
        std::error_code                              ec;
        for (const auto& d : DIRS) {
            const auto apps = d / "applications";
            if (!fs::is_directory(apps, ec))
                continue;
            for (auto it = fs::recursive_directory_iterator(apps, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (ec || it->path().extension() != ".desktop")
                    continue;
                std::ifstream in(it->path());
                std::string   line, icon, wmClass;
                bool          entry = false;
                while (std::getline(in, line)) {
                    if (line.starts_with('['))
                        entry = line == "[Desktop Entry]";
                    else if (entry && line.starts_with("Icon="))
                        icon = line.substr(5);
                    else if (entry && line.starts_with("StartupWMClass="))
                        wmClass = line.substr(15);
                }
                const auto FILE = resolve(icon, DIRS);
                if (FILE.empty())
                    continue;
                // Folders below applications/ make the id's parts (kde4/x.desktop: kde4-x).
                // Lexically: on NixOS the entries are links into the store.
                auto id = it->path().lexically_relative(apps).replace_extension().string();
                std::ranges::replace(id, '/', '-');
                const auto ID = lower(id);
                // The first data dir wins, as for desktop entries themselves.
                found.try_emplace(ID, FILE);
                if (!wmClass.empty())
                    found.try_emplace(lower(wmClass), FILE);
                if (const auto dot = ID.rfind('.'); dot != std::string::npos)
                    found.try_emplace(ID.substr(dot + 1), FILE);
            }
        }
        std::lock_guard l(idx->lock);
        idx->icons = std::move(found);
        idx->ready = true;
    }
}

void Nyx::Icons::start() {
    index = std::make_shared<SIndex>();
    // The environment is read here, on Hyprland's thread, not on the reader's.
    std::thread([idx = index, dirs = dataDirs()] { read(idx, dirs); }).detach();
}

bool Nyx::Icons::justReady() {
    return index && index->ready && !index->announced.exchange(true);
}

std::string Nyx::Icons::pathFor(const std::string& appId, const std::string& initialAppId) {
    if (!index || !index->ready)
        return "";
    std::lock_guard l(index->lock);
    for (const auto& id : {appId, initialAppId}) {
        if (const auto it = index->icons.find(lower(id)); it != index->icons.end())
            return it->second;
    }
    return "";
}

void Nyx::Icons::stop() {
    index.reset();
}
