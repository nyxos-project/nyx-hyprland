#!/usr/bin/env bash
# Rebuilds hyprland-nyx for a Hyprland release: Arch Linux's hyprland PKGBUILD for
# that release, with the Nyx patch series on top.
#
# Usage: ./update.sh [--force] [--no-build] [version]
#        ./update.sh --export <branch> [version]
#   version    an Arch package version (0.57.1-1) or a Hyprland version (0.57.1);
#              default: the hyprland version in the pacman sync database
#   --force    rebuild even if the PKGBUILD is already at that version
#   --no-build only regenerate the PKGBUILD (and the series, if it needed rebasing)
#   --export   write patches/<version>/ from the commits on <branch> after the
#              v<version> tag in the Hyprland checkout, then stop
#
# patches/<version>/ keeps one numbered series (0001-*.patch, 0002-*.patch, ...) per
# Hyprland version it was made for; the newest series that applies cleanly is used.
# If none does, the newest is rebased onto the new release in the Hyprland checkout
# ($HYPRLAND_SRC, default ~/Projects/Hyprland) with git's 3-way merge and saved as
# patches/<version>/. If that conflicts too, nothing is changed and the script stops.
#
# When Arch has no hyprland package for the version yet, its newest PKGBUILD is
# used with the version and source checksum replaced.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
arch="$here/arch"
hyprland_src=${HYPRLAND_SRC:-$HOME/Projects/Hyprland}
arch_repo=https://gitlab.archlinux.org/archlinux/packaging/packages/hyprland
arch_api=https://gitlab.archlinux.org/api/v4/projects/archlinux%2Fpackaging%2Fpackages%2Fhyprland

force=0 build=1 want= export_branch=
while [ $# -gt 0 ]; do
	case $1 in
	--force) force=1 ;;
	--no-build) build=0 ;;
	--export)
		[ $# -ge 2 ] || { echo "--export needs a branch" >&2; exit 2; }
		export_branch=$2
		shift
		;;
	-h | --help) sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	-*) echo "unknown option: $1" >&2; exit 2 ;;
	*) want=$1 ;;
	esac
	shift
done

die() { echo "error: $*" >&2; exit 1; }

# Writes the commits after v$1 on $2 to patches/$1/ as a numbered series.
export_series() {
	local ver=$1 branch=$2 dir="$here/patches/$1" tmp
	[ -d "$hyprland_src/.git" ] || die "no Hyprland checkout at $hyprland_src"
	git -C "$hyprland_src" rev-parse -q --verify "v$ver^{commit}" >/dev/null ||
		git -C "$hyprland_src" fetch -q origin tag "v$ver" --no-tags || die "couldn't fetch the v$ver tag"
	[ "$(git -C "$hyprland_src" rev-list --count "v$ver..$branch")" -gt 0 ] || die "$branch has no commits after v$ver"
	tmp=$(mktemp -d)
	git -C "$hyprland_src" format-patch -q --zero-commit -N -o "$tmp" "v$ver..$branch"
	rm -rf "$dir"
	mkdir -p "$dir"
	mv "$tmp"/*.patch "$dir/"
	rmdir "$tmp"
	echo "==> Wrote $(ls "$dir" | wc -l) patches from $branch to patches/$ver/"
}

if [ -n "$export_branch" ]; then
	[ -n "$want" ] || die "--export needs a version"
	export_series "${want%-*}" "$export_branch"
	exit 0
fi

# The Arch packaging tag to build from (0.57.1-1), and whether it's for another
# version (Arch hasn't packaged $ver yet).
arch_tags() {
	curl -sfL "$arch_api/repository/tags?search=$1&per_page=100" |
		python3 -c 'import json, sys; t = [x["name"] for x in json.load(sys.stdin)]; t.sort(key=lambda n: [int(p) for p in n.replace("-", ".").split(".") if p.isdigit()]); print(t[-1] if t else "")'
}
if [ -z "$want" ]; then
	tag=$(pacman -Si hyprland 2>/dev/null | awk '/^Version/ { print $3; exit }')
	[ -n "$tag" ] || die "pacman has no hyprland package in its sync database"
	tag=${tag#*:}
	ver=${tag%-*}
elif [[ $want == *-* ]]; then
	tag=$want
	ver=${tag%-*}
else
	ver=$want
	tag=$(arch_tags "%5E$ver-")
	if [ -z "$tag" ]; then
		tag=$(arch_tags "")
		[ -n "$tag" ] || die "couldn't list Arch's hyprland packages"
		echo "==> Arch has no hyprland package for $ver yet; adapting its $tag PKGBUILD"
	fi
fi

current=$(. "$arch/PKGBUILD" && echo "$pkgver")
if [ "$ver" = "$current" ] && [ "$force" = 0 ]; then
	echo "hyprland-nyx is already at $ver (use --force to rebuild)"
	exit 0
fi
echo "==> hyprland-nyx $current -> $ver (from Arch's hyprland $tag)"

work=$(mktemp -d "$here/.work.XXXXXX")
cleanup() {
	git -C "$hyprland_src" worktree remove --force "$work/wt" 2>/dev/null || true
	rm -rf "$work"
}
trap cleanup EXIT

curl -sfL -o "$work/PKGBUILD.arch" "$arch_repo/-/raw/$tag/PKGBUILD" || die "couldn't fetch Arch's PKGBUILD for $tag"

tarball="$arch/Hyprland-$ver.tar.gz"
if [ ! -f "$tarball" ]; then
	echo "==> Downloading Hyprland $ver"
	curl -sfL -o "$tarball.part" "https://github.com/hyprwm/Hyprland/releases/download/v$ver/source-v$ver.tar.gz" ||
		die "couldn't download the Hyprland $ver source"
	mv "$tarball.part" "$tarball"
fi
mkdir "$work/src"
tar -xzf "$tarball" -C "$work/src"
src="$work/src/hyprland-source"
[ -d "$src" ] || die "the $ver source tarball has no hyprland-source folder"

# A series applies when every patch applies in order with no fuzz: offsets are
# fine, guessed context is not.
applies() {
	rm -rf "$work/try"
	cp -a "$src" "$work/try"
	local p
	for p in "$1"/*.patch; do
		patch -d "$work/try" -Np1 -F0 --silent <"$p" >/dev/null 2>&1 || return 1
	done
}

mapfile -t series < <(find "$here/patches" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -rV)
[ "${#series[@]}" -gt 0 ] || die "no patch series in $here/patches"
chosen=
for s in "${series[@]}"; do
	if applies "$here/patches/$s"; then
		chosen=$s
		break
	fi
done

if [ -n "$chosen" ]; then
	echo "==> patches/$chosen applies to $ver"
else
	newest=${series[0]}
	echo "==> No saved series applies to $ver as is; rebasing patches/$newest in $hyprland_src"
	[ -d "$hyprland_src/.git" ] || die "no Hyprland checkout at $hyprland_src to rebase in"
	git -C "$hyprland_src" fetch -q origin tag "v$ver" --no-tags || die "couldn't fetch the v$ver tag"
	git -C "$hyprland_src" worktree add -q --detach "$work/wt" "v$ver"
	if ! git -C "$work/wt" am -q -3 "$here/patches/$newest"/*.patch; then
		git -C "$work/wt" am --abort 2>/dev/null || true
		die "the series conflicts with $ver; rebase it by hand (git -C $hyprland_src checkout -b nyx/$ver v$ver, then git am -3 patches/$newest/*.patch) and run ./update.sh --export nyx/$ver $ver"
	fi
	mkdir "$work/rebased"
	git -C "$work/wt" format-patch -q --zero-commit -N -o "$work/rebased" "v$ver..HEAD"
	applies "$work/rebased" || die "the rebased series still doesn't apply cleanly"
	mkdir -p "$here/patches/$ver"
	cp "$work/rebased"/*.patch "$here/patches/$ver/"
	chosen=$ver
	echo "==> Rebased the series onto $ver (patches/$ver)"
fi

mapfile -t patch_files < <(cd "$here/patches/$chosen" && ls -- *.patch | sort)
sha=$(sha256sum "$tarball" | cut -d' ' -f1)

# Arch's split package (hyprland + hyprpm) becomes one hyprland-nyx that replaces
# hyprland and provides it at the same version.
TAG=$tag VER=$ver SHA=$sha PATCHES="${patch_files[*]}" python3 - "$work/PKGBUILD.arch" "$work/PKGBUILD" <<'EOF'
import os, re, sys

text = open(sys.argv[1]).read()
patches, tag, ver, sha = os.environ["PATCHES"].split(), os.environ["TAG"], os.environ["VER"], os.environ["SHA"]

def sub(pattern, repl, flags=re.M):
    global text
    text, n = re.subn(pattern, lambda _: repl if isinstance(repl, str) else repl(_), text, count=1, flags=flags)
    if n != 1:
        sys.exit(f"Arch's PKGBUILD for {tag} changed shape (no match for {pattern!r}); update arch/PKGBUILD by hand")

sub(r"^pkgname=\(hyprland hyprpm\)$", "pkgname=hyprland-nyx\n_pkgname=hyprland")
sub(r"^pkgver=.*$", f"pkgver={ver}")
sub(r"^pkgrel=.*$", "pkgrel=1")
sub(r"^(pkgdesc='.*)'$", lambda m: m.group(1) + " (with Nyx patches)'")
sub(r"^url=.*$", "url='https://github.com/hyprwm/Hyprland'")
sub(r"^provides=\((.*)\)$", lambda m: f'provides=("$_pkgname=$pkgver" {m.group(1)})\nconflicts=("$_pkgname")')
sub(r'^_archive=.*$', '_archive="Hyprland-$pkgver"')
pad = "\n        "
sub(r"^source=\((\S+)\)$", lambda m: "source=(" + m.group(1) + "".join(pad + p for p in patches) + ")")
sub(r"^sha256sums=\('[0-9a-f]+'\)$", "sha256sums=('" + sha + "'" + "".join("\n            SKIP" for _ in patches) + ")")
apply = "".join(f'\tpatch -Np1 -i "$srcdir/{p}"\n' for p in patches)
sub(r'(^prepare\(\) \{\n(?:.*\n)*?\tcd "\$_archive"\n)', lambda m: m.group(1) + apply)
sub(r"^package_hyprland\(\) \{$", "package() {")
sub(r"\npackage_hyprpm\(\) \{\n.*?\n\}\n", "\n", re.S)
text = text.rstrip("\n") + "\n"

header = f"# Generated by update.sh from Arch Linux's hyprland {tag} PKGBUILD (LICENSE.PKGBUILD).\n"
open(sys.argv[2], "w").write(header + text)
EOF
bash -n "$work/PKGBUILD"
cp "$work/PKGBUILD" "$arch/PKGBUILD"
find "$arch" -maxdepth 1 -name '*.patch' -type l -delete
for p in "${patch_files[@]}"; do
	ln -sf "../patches/$chosen/$p" "$arch/$p"
done
find "$arch" -maxdepth 1 -name 'Hyprland-*.tar.gz' ! -name "Hyprland-$ver.tar.gz" -delete
echo "==> Wrote arch/PKGBUILD for $ver with ${#patch_files[@]} patches from patches/$chosen"

[ "$build" = 1 ] || exit 0

echo "==> Building (this takes a few minutes)"
mkdir -p "$arch/tmp"
(cd "$arch" && TMPDIR="$arch/tmp" makepkg -f --noconfirm) ||
	die "makepkg failed; if it's missing build dependencies, run: cd $arch && makepkg -s"

pkg=$(cd "$arch" && ls -t hyprland-nyx-"$ver"-1-*.pkg.tar.zst | grep -v -- -debug- | head -1)
echo
echo "Built $arch/$pkg. Install it, then log out and back in:"
echo "  sudo pacman -U $arch/$pkg"
