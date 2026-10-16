#!/bin/bash
# ---------------------------------------------------------------------------
# Build ATKPlayer.app and wrap it in a .dmg.
#
#   packaging/macos/build-dmg.sh [--arch arm64|x86_64] [--build-dir DIR]
#                                [--skip-build] [--output DIR] [--smoke-test]
#                                [--no-source-materials]
#
# Steps:
#   1. configure and build the macos-<arch>-release preset (unless --skip-build)
#   2. generate the dependency notices and corresponding source (Qt, FFmpeg,
#      vcpkg, ATK Player) with prepare-dependency-materials.py, exactly as
#      the Windows package does; --no-source-materials skips this for a quick
#      local test .dmg that must not be distributed. Needs Python 3.12+.
#      Then cmake --install into a staging folder; the install step runs
#      macdeployqt, which copies Qt and the FFmpeg dylibs into the bundle
#   3. remove Qt's own FFmpeg media plugin (ATK Player decodes with its own
#      FFmpeg; Qt Multimedia is only used for audio output), then make sure
#      every non-system library the bundle needs is inside it and referenced
#      through @rpath
#   4. sign inside out: every plugin, dylib and framework, then the app with
#      packaging/macos/ATKPlayer.entitlements. Ad hoc by default; Developer ID
#      (hardened runtime, secure timestamp) when ATK_CODESIGN_IDENTITY is set
#   5. verify with verify-app.sh: architectures, dependency closure, no
#      Homebrew or absolute paths, every file signed, strict verification,
#      hardened runtime and entitlements when expected
#   6. optionally launch the staged app and check that it stays up and maps
#      nothing from outside the bundle (--smoke-test)
#   7. hdiutil a compressed .dmg with an /Applications link, and sign it
#   8. notarise and staple the .dmg, only when ATK_NOTARY_PROFILE is set
#
# Environment:
#   ATK_CODESIGN_IDENTITY  "Developer ID Application: <name> (<team>)"; unset = ad hoc
#   ATK_HARDENED_RUNTIME   1 = hardened runtime even when ad hoc (CI uses this)
#   ATK_NOTARY_PROFILE     notarytool keychain profile; unset = no notarisation
#
# Needs QT_ROOT and VCPKG_ROOT for a build (see docs/BUILDING.md). Written for
# the bash 3.2 that ships with macOS.
#
# Signing and notarisation: like the Windows MSI, the default output is not
# signed with a Developer ID, but it is built to pass notarisation once one
# exists. Apple silicon refuses to run unsigned code at all, so by default it
# is signed ad hoc ("-"); Gatekeeper still blocks it on first launch until
# the user chooses Open from the Finder context menu (or System Settings >
# Privacy & Security > Open Anyway). See packaging/macos/README.md.
# ---------------------------------------------------------------------------
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"

arch="$(uname -m)"
build_dir=""
skip_build=0
output_dir="$repo/out/macos"
smoke_test=0
source_materials=1

while [ $# -gt 0 ]; do
    case "$1" in
        --arch) arch="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --skip-build) skip_build=1; shift ;;
        --output) output_dir="$2"; shift 2 ;;
        --smoke-test) smoke_test=1; shift ;;
        --no-source-materials) source_materials=0; shift ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

case "$arch" in
    arm64) preset="macos-arm64-release" ;;
    x86_64|x64) arch="x86_64"; preset="macos-x64-release" ;;
    *) echo "Unsupported architecture: $arch (use arm64 or x86_64)" >&2; exit 2 ;;
esac

[ -n "$build_dir" ] || build_dir="$repo/build/$preset"
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"

fail() { echo "error: $*" >&2; exit 1; }
step() { echo; echo "==> $*"; }

# ---------------------------------------------------------------------------
# 1. Build
# ---------------------------------------------------------------------------
if [ "$skip_build" -eq 0 ]; then
    [ -n "${QT_ROOT:-}" ] || fail "QT_ROOT is not set (e.g. ~/Qt/6.9.3/macos)"
    [ -n "${VCPKG_ROOT:-}" ] || fail "VCPKG_ROOT is not set"
    # The presets read this; an empty value would leave vcpkg without an
    # installed tree.
    export ATK_VCPKG_INSTALLED_DIR="${ATK_VCPKG_INSTALLED_DIR:-$build_dir/vcpkg_installed}"

    # -B so --build-dir is honoured; the preset still supplies everything else.
    step "Configuring $preset in $build_dir"
    (cd "$repo" && cmake --preset "$preset" -B "$build_dir" -DATK_VERSION_SUFFIX="${ATK_VERSION_SUFFIX-dev}")
    step "Building $build_dir"
    cmake --build "$build_dir"
fi

[ -f "$build_dir/CMakeCache.txt" ] || fail "No configured build at $build_dir"

cache_value() {
    sed -n "s/^$1:[A-Z]*=//p" "$build_dir/CMakeCache.txt" | head -n 1
}

ffmpeg_lib_dir=""
installed="$(cache_value VCPKG_INSTALLED_DIR)"
triplet="$(cache_value VCPKG_TARGET_TRIPLET)"
for candidate in "$installed/$triplet/lib" "$build_dir/vcpkg_installed/$triplet/lib"; do
    if [ -d "$candidate" ]; then ffmpeg_lib_dir="$candidate"; break; fi
done
[ -n "$ffmpeg_lib_dir" ] || fail "Could not find the vcpkg lib directory for triplet $triplet"

# ---------------------------------------------------------------------------
# 2. Licence materials, then install (macdeployqt runs as part of install)
# ---------------------------------------------------------------------------
materials_dir=""
if [ "$source_materials" -eq 1 ]; then
    qt_prefix="$(cache_value ATK_QT_ROOT)"
    [ -n "$qt_prefix" ] || qt_prefix="${QT_ROOT:-}"
    [ -n "$qt_prefix" ] || fail "Cannot find the Qt prefix (ATK_QT_ROOT in the cache, or QT_ROOT)"
    python="${PYTHON:-python3}"
    "$python" -c 'import sys; sys.exit(sys.version_info < (3, 12))' \
        || fail "$python is older than 3.12; set PYTHON to a newer one"
    # Both must be under build/: the script refuses to write anywhere else.
    materials_dir="$repo/build/dependency-materials-macos-$arch"
    step "Preparing dependency notices and corresponding source"
    "$python" "$repo/packaging/windows/prepare-dependency-materials.py" --platform macos \
        --qt-root "$qt_prefix" --ffmpeg-runtime "$ffmpeg_lib_dir" \
        --output "$materials_dir" --cache "$repo/build/dependency-cache"
else
    echo "warning: --no-source-materials: this .dmg lacks the corresponding source and must not be distributed" >&2
fi
# Recorded in the cache either way, so a later run without materials does not
# keep installing a stale set.
cmake "$build_dir" -DATK_DEPENDENCY_MATERIALS_DIR="$materials_dir" >/dev/null

stage="$output_dir/stage-$arch"
step "Installing into $stage"
rm -rf "$stage"
cmake --install "$build_dir" --prefix "$stage"

app="$stage/ATKPlayer.app"
contents="$app/Contents"
frameworks="$contents/Frameworks"
[ -x "$contents/MacOS/ATKPlayer" ] || fail "Install did not produce $app"
mkdir -p "$frameworks"

plist="$contents/Info.plist"
version="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$plist")"
suffix="$(cache_value ATK_VERSION_SUFFIX)"
[ -z "$suffix" ] || version="$version-$suffix"

# ---------------------------------------------------------------------------
# 3. Make the bundle self-contained
# ---------------------------------------------------------------------------
step "Removing Qt's FFmpeg media plugin"
find "$contents/PlugIns" -name 'libffmpegmediaplugin*.dylib' -print -delete 2>/dev/null || true

is_macho() { file -b "$1" | grep -q 'Mach-O'; }

macho_files() {
    find "$app" -type f | while IFS= read -r f; do
        if is_macho "$f"; then echo "$f"; fi
    done
}

# The list is written to a file and read on fd 3 so loop bodies run in this
# shell: variables they set survive and fail() stops the script.
macho_list="$output_dir/macho-$arch.txt"
refresh_macho_list() { macho_files > "$macho_list"; }

# Dependencies of one Mach-O file, without its own install name.
dependencies() {
    local f="$1" id
    id="$(otool -arch "$arch" -D "$f" 2>/dev/null | sed -n '2p')"
    # One architecture only: Qt's frameworks are universal, and otool would
    # otherwise list every slice under its own header line.
    otool -arch "$arch" -L "$f" | sed -n '2,$p' | awk '{print $1}' | while IFS= read -r dep; do
        [ "$dep" = "$id" ] || echo "$dep"
    done
}

is_system() {
    case "$1" in
        /System/*|/usr/lib/*) return 0 ;;
        *) return 1 ;;
    esac
}

# Where a dependency of $2 resolves inside the bundle, or empty.
resolve() {
    local dep="$1" from="$2" rest
    case "$dep" in
        @rpath/*)
            rest="${dep#@rpath/}"
            if [ -e "$frameworks/$rest" ]; then echo "$frameworks/$rest"; fi ;;
        @executable_path/*)
            rest="${dep#@executable_path/}"
            if [ -e "$contents/MacOS/$rest" ]; then echo "$contents/MacOS/$rest"; fi ;;
        @loader_path/*)
            rest="${dep#@loader_path/}"
            if [ -e "$(dirname "$from")/$rest" ]; then echo "$(dirname "$from")/$rest"; fi ;;
        "$app"/*)
            if [ -e "$dep" ]; then echo "$dep"; fi ;;
    esac
}

# Our FFmpeg, never another copy that happens to share a file name.
step "Using the FFmpeg dylibs this build linked against"
for lib in "$frameworks"/*.dylib; do
    [ -e "$lib" ] || continue
    name="$(basename "$lib")"
    if [ -e "$ffmpeg_lib_dir/$name" ]; then
        echo "  = $name"
        rm -f "$lib"
        cp -L "$ffmpeg_lib_dir/$name" "$lib"
        chmod u+w "$lib"
        install_name_tool -id "@rpath/$name" "$lib"
    fi
done

step "Bringing every non-system dependency into Contents/Frameworks"
pass=0
while :; do
    pass=$((pass + 1))
    [ "$pass" -le 10 ] || fail "Dependency fix-up did not settle after 10 passes"
    changed=0
    refresh_macho_list
    while IFS= read -r f <&3; do
        for dep in $(dependencies "$f"); do
            is_system "$dep" && continue
            [ -n "$(resolve "$dep" "$f")" ] && continue

            name="$(basename "$dep")"
            source=""
            case "$dep" in
                /*) [ -e "$dep" ] && source="$dep" ;;
            esac
            [ -n "$source" ] || { [ -e "$ffmpeg_lib_dir/$name" ] && source="$ffmpeg_lib_dir/$name"; }
            [ -n "$source" ] || fail "$(basename "$f") needs $dep, which is not in the bundle or $ffmpeg_lib_dir"

            if [ ! -e "$frameworks/$name" ]; then
                echo "  + $name  (from $source)"
                cp -L "$source" "$frameworks/$name"
                chmod u+w "$frameworks/$name"
                install_name_tool -id "@rpath/$name" "$frameworks/$name"
            fi
            echo "  ~ $(basename "$f"): $dep -> @rpath/$name"
            install_name_tool -change "$dep" "@rpath/$name" "$f"
            changed=1
        done
    done 3< "$macho_list"
    [ "$changed" -eq 1 ] || break
done

step "Pruning unreferenced dylibs left by the removed plugin"
while :; do
    removed=0
    refresh_macho_list
    all_deps="$(while IFS= read -r f; do dependencies "$f"; done < "$macho_list" | sort -u)"
    for lib in "$frameworks"/*.dylib; do
        [ -e "$lib" ] || continue
        name="$(basename "$lib")"
        if ! echo "$all_deps" | grep -q "/$name\$"; then
            echo "  - $name"
            rm -f "$lib"
            removed=1
        fi
    done
    [ "$removed" -eq 1 ] || break
done

step "Removing build-machine RPATHs"
refresh_macho_list
while IFS= read -r f <&3; do
    for rp in $(otool -arch "$arch" -l "$f" | awk '/LC_RPATH/{getline; getline; print $2}'); do
        case "$rp" in
            @*) ;;
            *) echo "  - $(basename "$f"): $rp"; install_name_tool -delete_rpath "$rp" "$f" ;;
        esac
    done
done 3< "$macho_list"

# ---------------------------------------------------------------------------
# 4. Sign, inside out
# ---------------------------------------------------------------------------
# With ATK_CODESIGN_IDENTITY set (a "Developer ID Application: ..." identity)
# everything is signed for notarisation: hardened runtime, secure timestamp,
# the checked-in entitlements. Without it the signature is ad hoc; the
# hardened runtime can still be switched on with ATK_HARDENED_RUNTIME=1, which
# is how CI exercises the notarisation path before there is a certificate.
identity="${ATK_CODESIGN_IDENTITY:--}"
hardened="${ATK_HARDENED_RUNTIME:-0}"
[ "$identity" = "-" ] || hardened=1
entitlements="$here/ATKPlayer.entitlements"
plutil -lint "$entitlements" >/dev/null || fail "$entitlements is not a valid property list"

sign_args=(--force --sign "$identity")
if [ "$identity" = "-" ]; then
    sign_args+=(--timestamp=none)
    step "Signing ad hoc (no Developer ID; see packaging/macos/README.md)"
else
    # A secure timestamp is required for notarisation.
    sign_args+=(--timestamp)
    step "Signing with $identity"
fi
if [ "$hardened" = "1" ]; then
    sign_args+=(--options runtime)
    echo "  hardened runtime: on"
fi

# Every Mach-O file that is not a framework's own binary or the app's
# executable, deepest first: plugins, loose dylibs, any helper tools.
refresh_macho_list
while IFS= read -r f; do
    case "$f" in
        "$contents/MacOS/ATKPlayer") continue ;;
        */*.framework/Versions/*/*)
            # The framework's main binary is signed with its bundle below;
            # anything else inside a framework (helpers) is signed here.
            rest="${f#*.framework/Versions/}"; rest="${rest#*/}"
            fw="${f%%.framework/*}"; fw="${fw##*/}"
            [ "$rest" = "$fw" ] && continue ;;
    esac
    echo "$(echo "$f" | tr -cd '/' | wc -c) $f"
done < "$macho_list" | sort -rn | cut -d' ' -f2- > "$macho_list.sign"
while IFS= read -r f <&3; do
    codesign "${sign_args[@]}" "$f"
done 3< "$macho_list.sign"
echo "  signed $(wc -l < "$macho_list.sign" | tr -d ' ') plugins and libraries"
rm -f "$macho_list.sign"

find "$frameworks" -maxdepth 1 -name '*.framework' > "$macho_list"
while IFS= read -r f <&3; do
    codesign "${sign_args[@]}" "$f"
done 3< "$macho_list"
echo "  signed $(wc -l < "$macho_list" | tr -d ' ') frameworks"

# The app last, and only the app carries entitlements. No --deep: it would
# re-sign nested code with the app's options instead of its own.
codesign "${sign_args[@]}" --entitlements "$entitlements" "$app"

# ---------------------------------------------------------------------------
# 5. Verify
# ---------------------------------------------------------------------------
step "Verifying"
verify_args=("$app" "$arch")
[ "$hardened" = "1" ] && verify_args+=(--expect-hardened)
[ "$source_materials" -eq 1 ] && verify_args+=(--expect-source-materials)
"$here/verify-app.sh" "${verify_args[@]}"
rm -f "$macho_list"
ls "$frameworks"

# ---------------------------------------------------------------------------
# 6. Smoke test
# ---------------------------------------------------------------------------
if [ "$smoke_test" -eq 1 ]; then
    step "Launching the staged app"
    log="$output_dir/smoke-$arch.log"
    # A CI runner's Qt installer exports plugin paths; with them the app would
    # load the installer's plugins and the check below could not trust itself.
    env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
        -u DYLD_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH \
        "$contents/MacOS/ATKPlayer" >"$log" 2>&1 &
    pid=$!
    sleep 10
    if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" || code=$?
        tail -n 40 "$log"
        fail "ATKPlayer exited on startup with code ${code:-0}"
    fi

    # Every non-system image the running process has mapped must come from
    # the bundle; anything else would break on a Mac without Qt or vcpkg.
    # lsof rather than DYLD_PRINT_LIBRARIES, which the hardened runtime
    # ignores.
    images="$output_dir/smoke-$arch-images.txt"
    lsof -p "$pid" -Fn 2>/dev/null | sed -n 's/^n//p' \
        | grep -E '\.dylib$|\.framework/|/MacOS/' | sort -u > "$images" || true
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    grep -q "^$app/Contents/Frameworks/" "$images" || fail "lsof listed no bundled libraries; cannot check"
    outside="$(grep -v -e '^/System/' -e '^/usr/lib/' -e "^$app/" -e '^/Library/Apple/' \
        -e '^/private/var/db/' -e '^/usr/share/' "$images" || true)"
    if [ -n "$outside" ]; then
        echo "$outside"
        fail "The app loaded libraries from outside its bundle"
    fi
    echo "  ATKPlayer started, stayed running and loaded $(grep -c "^$app/" "$images") bundled images and nothing else outside the OS"
fi

# ---------------------------------------------------------------------------
# 7. Disk image
# ---------------------------------------------------------------------------
dmg_name="ATKPlayer-$version-macOS-$arch.dmg"
dmg="$output_dir/$dmg_name"
root="$output_dir/dmg-$arch"
step "Creating $dmg_name"
rm -rf "$root" "$dmg"
mkdir -p "$root"
cp -R "$app" "$root/"
ln -s /Applications "$root/Applications"
cp "$here/READ_ME_FIRST.txt" "$root/Read Me First.txt"
hdiutil create -volname "ATK Player $version" -srcfolder "$root" -ov -format UDZO "$dmg"
rm -rf "$root"

# The disk image carries its own signature (no hardened runtime or
# entitlements: those apply to code, not containers).
dmg_sign_args=(--force --sign "$identity")
if [ "$identity" = "-" ]; then dmg_sign_args+=(--timestamp=none); else dmg_sign_args+=(--timestamp); fi
codesign "${dmg_sign_args[@]}" "$dmg"
codesign --verify --strict --verbose=2 "$dmg"

# ---------------------------------------------------------------------------
# 8. Notarise and staple (only with ATK_NOTARY_PROFILE)
# ---------------------------------------------------------------------------
# ATK_NOTARY_PROFILE names credentials stored once with
#   xcrun notarytool store-credentials <profile> --apple-id ... --team-id ...
# See packaging/macos/README.md.
if [ -n "${ATK_NOTARY_PROFILE:-}" ]; then
    [ "$identity" != "-" ] || fail "ATK_NOTARY_PROFILE is set but the build is signed ad hoc; set ATK_CODESIGN_IDENTITY"
    step "Notarising $dmg_name with profile $ATK_NOTARY_PROFILE"
    xcrun notarytool submit "$dmg" --keychain-profile "$ATK_NOTARY_PROFILE" --wait \
        --output-format json > "$output_dir/notary-$arch.json" || true
    cat "$output_dir/notary-$arch.json"
    if ! grep -q '"status" *: *"Accepted"' "$output_dir/notary-$arch.json"; then
        submission="$(sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' "$output_dir/notary-$arch.json" | head -n 1)"
        [ -z "$submission" ] || xcrun notarytool log "$submission" --keychain-profile "$ATK_NOTARY_PROFILE" || true
        fail "Notarisation was not accepted"
    fi
    xcrun stapler staple "$dmg"
    xcrun stapler validate "$dmg"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
fi

(cd "$output_dir" && shasum -a 256 "$dmg_name" > "$dmg_name.sha256")

echo
echo "Created $dmg"
cat "$dmg.sha256"
