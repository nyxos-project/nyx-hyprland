#!/usr/bin/env bash
# Rebuilds hyprland-nyx for a Hyprland release: Arch Linux's hyprland PKGBUILD for
# that release, with the Nyx patch on top.
#
# Usage: ./update.sh [--force] [--no-build] [version]
#   version    an Arch package version (0.57.1-1) or a Hyprland version (0.57.1);
#              default: the hyprland version in the pacman sync database
#   --force    rebuild even if the PKGBUILD is already at that version
#   --no-build only regenerate the PKGBUILD (and the patch, if it needed rebasing)
#
# patches/ keeps one patch per Hyprland version it was made for; the newest one that
# applies cleanly is used. If none does, the newest is rebased onto the new release
# in the Hyprland checkout ($HYPRLAND_SRC, default ~/Projects/Hyprland) with git's
# 3-way merge and saved as patches/workspace-swipe-wrap-<version>.patch. If that
# conflicts too, nothing is changed and the script stops.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
arch="$here/arch"
patch_name=0001-gestures-add-workspace_swipe_wrap.patch
hyprland_src=${HYPRLAND_SRC:-$HOME/Projects/Hyprland}
arch_repo=https://gitlab.archlinux.org/archlinux/packaging/packages/hyprland
arch_api=https://gitlab.archlinux.org/api/v4/projects/archlinux%2Fpackaging%2Fpackages%2Fhyprland

force=0 build=1 want=
for arg in "$@"; do
	case $arg in
	--force) force=1 ;;
	--no-build) build=0 ;;
	-h | --help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	-*) echo "unknown option: $arg" >&2; exit 2 ;;
	*) want=$arg ;;
	esac
done

die() { echo "error: $*" >&2; exit 1; }

# The Arch packaging tag to build from (0.57.1-1).
if [ -z "$want" ]; then
	tag=$(pacman -Si hyprland 2>/dev/null | awk '/^Version/ { print $3; exit }')
	[ -n "$tag" ] || die "pacman has no hyprland package in its sync database"
	tag=${tag#*:}
elif [[ $want == *-* ]]; then
	tag=$want
else
	tag=$(curl -sfL "$arch_api/repository/tags?search=%5E$want-&per_page=100" |
		python3 -c 'import json, sys; t = [x["name"] for x in json.load(sys.stdin)]; t.sort(key=lambda n: int(n.rsplit("-", 1)[1])); print(t[-1] if t else "")')
	[ -n "$tag" ] || die "Arch has no hyprland package for $want"
fi
ver=${tag%-*}

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

# Applies with no fuzz: offsets are fine, guessed context is not.
applies() { patch -d "$src" -Np1 -F0 --dry-run --silent <"$1" >/dev/null 2>&1; }

mapfile -t patches < <(ls "$here"/patches/workspace-swipe-wrap-*.patch | sort -rV)
[ "${#patches[@]}" -gt 0 ] || die "no patches in $here/patches"
patch=
for p in "${patches[@]}"; do
	if applies "$p"; then
		patch=$p
		break
	fi
done

if [ -n "$patch" ]; then
	echo "==> $(basename "$patch") applies to $ver"
else
	newest=${patches[0]}
	patch="$here/patches/workspace-swipe-wrap-$ver.patch"
	echo "==> No saved patch applies to $ver as is; rebasing $(basename "$newest") in $hyprland_src"
	[ -d "$hyprland_src/.git" ] || die "no Hyprland checkout at $hyprland_src to rebase in"
	git -C "$hyprland_src" fetch -q origin tag "v$ver" --no-tags || die "couldn't fetch the v$ver tag"
	git -C "$hyprland_src" worktree add -q --detach "$work/wt" "v$ver"
	if ! git -C "$work/wt" am -q -3 <"$newest"; then
		git -C "$work/wt" am --abort 2>/dev/null || true
		die "the patch conflicts with $ver; rebase it by hand (git -C $hyprland_src checkout -b workspace-swipe-wrap-$ver v$ver, then git am -3 $newest) and save it as $patch"
	fi
	git -C "$work/wt" format-patch -q -1 --zero-commit -N --stdout >"$work/rebased.patch"
	applies "$work/rebased.patch" || die "the rebased patch still doesn't apply cleanly"
	cp "$work/rebased.patch" "$patch"
	echo "==> Rebased the patch onto $ver ($patch)"
fi

# Arch's split package (hyprland + hyprpm) becomes one hyprland-nyx that replaces
# hyprland and provides it at the same version.
TAG=$tag PATCH=$patch_name python3 - "$work/PKGBUILD.arch" "$work/PKGBUILD" <<'EOF'
import os, re, sys

text = open(sys.argv[1]).read()
patch, tag = os.environ["PATCH"], os.environ["TAG"]

def sub(pattern, repl, flags=re.M):
    global text
    text, n = re.subn(pattern, repl, text, count=1, flags=flags)
    if n != 1:
        sys.exit(f"Arch's PKGBUILD for {tag} changed shape (no match for {pattern!r}); update arch/PKGBUILD by hand")

sub(r"^pkgname=\(hyprland hyprpm\)$", "pkgname=hyprland-nyx\n_pkgname=hyprland")
sub(r"^pkgrel=.*$", "pkgrel=1")
sub(r"^(pkgdesc='.*)'$", r"\1 (with Nyx patches)'")
sub(r"^url=.*$", "url='https://github.com/hyprwm/Hyprland'")
sub(r"^provides=\((.*)\)$", r'provides=("$_pkgname=$pkgver" \1)' + '\nconflicts=("$_pkgname")')
sub(r'^_archive=.*$', '_archive="Hyprland-$pkgver"')
sub(r"^source=\((.*)\)$", r"source=(\1" + f"\n        {patch})")
sub(r"^sha256sums=\((.*)\)$", r"sha256sums=(\1" + "\n            SKIP)")
sub(r'(^prepare\(\) \{\n(?:.*\n)*?\tcd "\$_archive"\n)', r"\1" + f'\tpatch -Np1 -i "$srcdir/{patch}"\n')
sub(r"^package_hyprland\(\) \{$", "package() {")
sub(r"\npackage_hyprpm\(\) \{\n.*?\n\}\n", "\n", re.S)
text = text.rstrip("\n") + "\n"

header = f"# Generated by update.sh from Arch Linux's hyprland {tag} PKGBUILD (LICENSE.PKGBUILD).\n"
open(sys.argv[2], "w").write(header + text)
EOF
bash -n "$work/PKGBUILD"
cp "$work/PKGBUILD" "$arch/PKGBUILD"
ln -sf "../patches/$(basename "$patch")" "$arch/$patch_name"
find "$arch" -maxdepth 1 -name 'Hyprland-*.tar.gz' ! -name "Hyprland-$ver.tar.gz" -delete
echo "==> Wrote arch/PKGBUILD for $ver"

[ "$build" = 1 ] || exit 0

echo "==> Building (this takes a few minutes)"
mkdir -p "$arch/tmp"
(cd "$arch" && TMPDIR="$arch/tmp" makepkg -f --noconfirm) ||
	die "makepkg failed; if it's missing build dependencies, run: cd $arch && makepkg -s"

pkg=$(cd "$arch" && ls -t hyprland-nyx-"$ver"-1-*.pkg.tar.zst | grep -v -- -debug- | head -1)
echo
echo "Built $arch/$pkg. Install it, then log out and back in:"
echo "  sudo pacman -U $arch/$pkg"
