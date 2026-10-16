#!/bin/bash
# ---------------------------------------------------------------------------
# Check that ATKPlayer.app is self-contained and signed the way Apple's
# notary service requires.
#
#   packaging/macos/verify-app.sh <ATKPlayer.app> <arch> [--expect-hardened]
#
# Fails when:
#   - a Mach-O file lacks the <arch> slice
#   - a non-system dependency does not resolve inside the bundle
#   - any dependency or RPATH points at /opt/homebrew, /usr/local or another
#     absolute path outside the bundle and the OS
#   - any Mach-O file is unsigned, or the bundle fails
#     codesign --verify --deep --strict
#   - with --expect-hardened: the executable is not signed with the hardened
#     runtime, or its entitlements are not the checked-in ATKPlayer.entitlements
#
# build-dmg.sh runs it on the staged app; CI runs it again on the app inside
# the finished disk image. Written for the bash 3.2 that ships with macOS.
# ---------------------------------------------------------------------------
set -euo pipefail

[ $# -ge 2 ] || { echo "usage: $0 <ATKPlayer.app> <arch> [--expect-hardened]" >&2; exit 2; }
app="$(cd "$1" && pwd)"
arch="$2"
expect_hardened=0
[ "${3:-}" = "--expect-hardened" ] && expect_hardened=1

here="$(cd "$(dirname "$0")" && pwd)"
contents="$app/Contents"
frameworks="$contents/Frameworks"
executable="$contents/MacOS/ATKPlayer"
[ -x "$executable" ] || { echo "error: $executable not found" >&2; exit 1; }

list="$(mktemp -t atk-macho)"
trap 'rm -f "$list"' EXIT
find "$app" -type f | while IFS= read -r f; do
    if file -b "$f" | grep -q 'Mach-O'; then echo "$f"; fi
done > "$list"

dependencies() {
    local f="$1" id
    id="$(otool -arch "$arch" -D "$f" 2>/dev/null | sed -n '2p')"
    otool -arch "$arch" -L "$f" | sed -n '2,$p' | awk '{print $1}' | while IFS= read -r dep; do
        [ "$dep" = "$id" ] || echo "$dep"
    done
}

resolve() {
    local dep="$1" from="$2" rest
    case "$dep" in
        @rpath/*) rest="${dep#@rpath/}"; [ -e "$frameworks/$rest" ] && echo ok ;;
        @executable_path/*) rest="${dep#@executable_path/}"; [ -e "$contents/MacOS/$rest" ] && echo ok ;;
        @loader_path/*) rest="${dep#@loader_path/}"; [ -e "$(dirname "$from")/$rest" ] && echo ok ;;
    esac
    return 0
}

problems=0
problem() { echo "  ! $*"; problems=1; }

echo "Checking $(wc -l < "$list" | tr -d ' ') Mach-O files in ${app##*/} for $arch"
while IFS= read -r f <&3; do
    short="${f#$app/}"
    if ! lipo -archs "$f" | tr ' ' '\n' | grep -qx "$arch"; then
        problem "$short has no $arch slice ($(lipo -archs "$f"))"
    fi
    for dep in $(dependencies "$f"); do
        case "$dep" in
            /System/*|/usr/lib/*) continue ;;
            /opt/homebrew/*|/usr/local/*) problem "$short links $dep (Homebrew or /usr/local)"; continue ;;
            /*) problem "$short links $dep (absolute path outside the bundle)"; continue ;;
        esac
        [ -n "$(resolve "$dep" "$f")" ] || problem "$short -> $dep does not resolve inside the bundle"
    done
    for rp in $(otool -arch "$arch" -l "$f" | awk '/LC_RPATH/{getline; getline; print $2}'); do
        case "$rp" in
            @*) ;;
            *) problem "$short has RPATH $rp" ;;
        esac
    done
    if ! codesign --verify --strict "$f" 2>/dev/null; then
        problem "$short is not signed (or its signature is invalid)"
    fi
done 3< "$list"

if ! codesign --verify --deep --strict --verbose=2 "$app"; then
    problem "codesign --verify --deep --strict failed for the bundle"
fi

info="$(codesign --display --verbose=4 "$app" 2>&1)"
echo "$info" | grep -E '^(Identifier|Format|CodeDirectory|Signature|Authority|TeamIdentifier|Timestamp|Runtime Version)' || true

if [ "$expect_hardened" -eq 1 ]; then
    # CodeDirectory flags carry "runtime" when signed with --options runtime.
    if echo "$info" | grep -E '^CodeDirectory' | grep -q 'runtime'; then
        echo "  hardened runtime: on"
    else
        problem "the executable is not signed with the hardened runtime"
    fi
    # The signed entitlements must be exactly the checked-in file. An empty
    # set may be recorded as an empty dictionary or not at all.
    signed="$(mktemp -t atk-ent)"
    codesign --display --entitlements - --xml "$app" 2>/dev/null > "$signed" || true
    expected="$(plutil -p "$here/ATKPlayer.entitlements")"
    actual="$(plutil -p "$signed" 2>/dev/null || true)"
    rm -f "$signed"
    if [ "$expected" = "$actual" ] || { [ "$expected" = "{
}" ] && [ -z "$actual" ]; }; then
        echo "  entitlements: match ATKPlayer.entitlements"
    else
        echo "  signed entitlements: ${actual:-<none>}"
        problem "the signed entitlements differ from packaging/macos/ATKPlayer.entitlements"
    fi
fi

if [ "$problems" -ne 0 ]; then
    echo "error: ${app##*/} would not pass Developer ID signing and notarisation checks" >&2
    exit 1
fi
echo "  ${app##*/} is self-contained, fully signed and passes strict verification"
