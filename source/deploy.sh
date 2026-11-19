#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Build, bundle and smoke-test the standalone MSPA binary.
#
# Usage: ./source/deploy.sh <target>
#
# The script builds the static miallib library, compiles the MSPA driver,
# creates a self-contained distribution directory and runs a test image
# through the freshly built binary.
# ---------------------------------------------------------------------------
set -euo pipefail

if [ -z "${BASH_VERSION:-}" ]; then
    echo "ERROR: this script must be run with bash (e.g. ./source/deploy.sh Linux)."
    exit 1
fi

trap 'echo "ERROR: build failed at line $LINENO." >&2' ERR

base_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$base_dir"

# Minimum macOS version of the macOS build. The makefile applies this to the
# compiler and linker; it is exported here so that the value used for the
# check below is the same.
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"

usage() {
    cat <<'EOF'
Usage: ./source/deploy.sh <target>

Targets:
  Linux         Linux x86_64 (glibc based, e.g. Debian/Ubuntu)
  Linux_ARM     Linux aarch64 / ARM64
  macOS_ARM     macOS on Apple Silicon (Homebrew GCC)
  Windows       MS-Windows x86_64 (MSYS2 UCRT64)
  Windows_ARM   MS-Windows ARM64 (MSYS2 CLANGARM64)

  -h, --help    Show this help and exit.

The target must match the host operating system and CPU architecture
(the makefiles do not cross-compile).
EOF
}

target="${1:-}"
case "$target" in
    -h|--help|help)
        usage
        exit 0
        ;;
esac

if [ -z "$target" ]; then
    usage
    exit 1
fi

MAKEFILE=""
BINARY_NAME=""
DIST_DIR=""
IS_MAC=false
LIB_CC="${MSPA_CC:-}"
EXTRA_CFLAGS=""

case "$target" in
    "Linux")
        MAKEFILE="Makefile_Linux";        BINARY_NAME="mspa_linux";         DIST_DIR="mspa_dist_linux"
        LIB_CC="${MSPA_CC:-gcc}"
        ;;
    "Linux_ARM")
        MAKEFILE="Makefile_Linux_ARM";    BINARY_NAME="mspa_linux_ARM";     DIST_DIR="mspa_dist_linux_ARM"
        LIB_CC="${MSPA_CC:-gcc}"
        ;;
    "macOS_ARM")
        MAKEFILE="Makefile_macOS_ARM";    BINARY_NAME="mspa_macOS_ARM";     DIST_DIR="mspa_dist_macOS_ARM"
        IS_MAC=true
        ;;
    "Windows")
        MAKEFILE="Makefile_Windows";      BINARY_NAME="mspa_win64.exe";     DIST_DIR="mspa_dist_windows"
        LIB_CC="${MSPA_CC:-gcc}"
        ;;
    "Windows_ARM")
        MAKEFILE="Makefile_Windows_ARM";  BINARY_NAME="mspa_win64_ARM.exe"; DIST_DIR="mspa_dist_windows_ARM"
        LIB_CC="${MSPA_CC:-gcc}"
        ;;
    *)
        echo "Unknown target: $target"
        usage
        exit 1
        ;;
esac

BUILD_BIN="source/$BINARY_NAME"
DIST_DIR="source/$DIST_DIR"

# --- Host / target compatibility -------------------------------------------
# The makefiles do not cross-compile: a target must be built on the platform
# it is meant for. Without this check, for example ./source/deploy.sh Linux_ARM on
# an x86_64 host would silently produce an x86_64 binary called
# mspa_linux_ARM.
target_os() {
    case "$1" in
        Linux|Linux_ARM)     echo "Linux" ;;
        macOS_ARM)           echo "macOS" ;;
        Windows|Windows_ARM) echo "Windows" ;;
    esac
}

target_arch() {
    case "$1" in
        Linux|Windows) echo "x86_64" ;;
        *)             echo "arm64" ;;
    esac
}

host_os="$(uname -s 2>/dev/null || echo unknown)"
case "$host_os" in
    Linux*)               host_os="Linux" ;;
    Darwin*)              host_os="macOS" ;;
    MINGW*|MSYS*|CYGWIN*) host_os="Windows" ;;
esac

host_arch="$(uname -m 2>/dev/null || echo unknown)"
case "$host_arch" in
    x86_64|amd64)  host_arch="x86_64" ;;
    arm64|aarch64) host_arch="arm64" ;;
esac

# In MSYS2, $MSYSTEM identifies the environment more reliably than uname
if [ "$host_os" = "Windows" ]; then
    case "${MSYSTEM:-}" in
        CLANGARM64)                  host_arch="arm64" ;;
        UCRT64|MINGW64|CLANG64|MSYS) host_arch="x86_64" ;;
    esac
fi

want_os="$(target_os "$target")"
want_arch="$(target_arch "$target")"

if [ "$host_os" != "$want_os" ] || [ "$host_arch" != "$want_arch" ]; then
    echo "ERROR: target '$target' must be built on $want_os/$want_arch,"
    echo "       but this machine is $host_os/$host_arch."
    echo

    if [ "$host_os/$host_arch" = "macOS/x86_64" ]; then
        echo "This machine (macOS/x86_64) has no supported target:"
        echo "macOS on Intel is no longer supported. Use an Apple Silicon Mac"
        echo "for the macOS_ARM target."
    else
        echo "Targets that match this machine:"
        found=false
        for candidate in Linux Linux_ARM macOS_ARM Windows Windows_ARM; do
            if [ "$(target_os "$candidate")/$(target_arch "$candidate")" = "$host_os/$host_arch" ]; then
                echo "  ./source/deploy.sh $candidate"
                found=true
            fi
        done
        if [ "$found" = false ]; then
            echo "  (none: this platform/architecture is not supported)"
        fi
    fi
    exit 1
fi

if [ ! -f "source/$MAKEFILE" ]; then
    echo "ERROR: source/$MAKEFILE not found in $base_dir"
    exit 1
fi

# Pixel comparison tool (no Python/ImageMagick dependency, uses libtiff)
case "$target" in
    "Windows"|"Windows_ARM") TIFFCMP="source/testing/tiffcmp.exe" ;;
    *)                       TIFFCMP="source/testing/tiffcmp" ;;
esac

# Number of parallel compile jobs (portable across Linux, macOS and MSYS2)
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
case "$jobs" in
    ''|*[!0-9]*) jobs=4 ;;
esac

echo "==> Target:          $target"
echo "==> Host:            $host_os/$host_arch"
echo "==> Base directory:  $base_dir"
echo "==> Distribution:    $DIST_DIR/"

if [ "$IS_MAC" = true ]; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "ERROR: Homebrew not found. Install it from https://brew.sh"
        exit 1
    fi
    BREW_PREFIX="$(brew --prefix)"
    EXTRA_CFLAGS="-I$BREW_PREFIX/include"

    if [ -z "$LIB_CC" ]; then
        # Use whichever Homebrew GCC is installed; no version is hardcoded.
        gcc_version="$(brew list --versions gcc 2>/dev/null | awk 'NR==1 {print $2}' || true)"
        gcc_major="${gcc_version%%.*}"
        if [ -n "$gcc_major" ] && [ -x "$BREW_PREFIX/bin/gcc-$gcc_major" ]; then
            LIB_CC="$BREW_PREFIX/bin/gcc-$gcc_major"
        else
            # Fallback: pick the highest gcc-N in the Homebrew bin directory
            best_major=""
            for candidate in "$BREW_PREFIX"/bin/gcc-[0-9]*; do
                [ -x "$candidate" ] || continue
                major="${candidate##*/gcc-}"
                case "$major" in
                    ''|*[!0-9]*) continue ;;
                esac
                if [ -z "$best_major" ] || [ "$major" -gt "$best_major" ]; then
                    best_major="$major"
                    LIB_CC="$candidate"
                fi
            done
        fi
    fi

    if [ -z "$LIB_CC" ] || ! command -v "$LIB_CC" >/dev/null 2>&1; then
        echo "ERROR: no Homebrew GCC found (brew install gcc), or MSPA_CC does not name a compiler."
        exit 1
    fi
    echo "==> Homebrew:        $BREW_PREFIX"
fi

if ! command -v "$LIB_CC" >/dev/null 2>&1; then
    echo "ERROR: compiler $LIB_CC not found."
    exit 1
fi

cc_version="$("$LIB_CC" --version 2>/dev/null | head -n 1 || true)"
echo "==> C compiler:      $LIB_CC${cc_version:+ ($cc_version)}"

# 1. Build the static miallib library and copy it next to this script
echo "==> Building libmiallib_mspa.a with $LIB_CC"
make -C source/mial-master/core/c clean
make -C source/mial-master/core/c -j"$jobs" \
     CC="$LIB_CC" EXTRA_CFLAGS="$EXTRA_CFLAGS" \
     ../build/lib/libmiallib_mspa.a
if [ ! -f source/mial-master/core/build/lib/libmiallib_mspa.a ]; then
    echo "ERROR: libmiallib_mspa.a was not created."
    exit 1
fi
cp -f source/mial-master/core/build/lib/libmiallib_mspa.a source/

# 2. Compile the driver
echo "==> Compiling source/$BINARY_NAME with source/$MAKEFILE"
make -C source -f "$MAKEFILE" clean
make -C source -f "$MAKEFILE" CC="$LIB_CC"

if [ ! -f "$BUILD_BIN" ]; then
    echo "ERROR: $BUILD_BIN was not created."
    exit 1
fi

# Second line of defence: the host check above already rejects an impossible
# target, but a misconfigured compiler (e.g. an Intel GCC under Rosetta on an
# Apple Silicon Mac) could still produce a binary for the wrong CPU.
if command -v file >/dev/null 2>&1; then
    case "$target" in
        Linux|Windows) arch_pattern="x86-64" ;;
        *)             arch_pattern="aarch64|arm64" ;;
    esac
    if ! file "$BUILD_BIN" | grep -Eqi "$arch_pattern"; then
        echo "ERROR: $BUILD_BIN was built for the wrong CPU architecture:"
        file "$BUILD_BIN"
        echo "Expected $want_arch (matching '$arch_pattern')."
        exit 1
    fi
fi

# 3. Create the distribution directory
echo "==> Creating $DIST_DIR/"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
cp -f "$BUILD_BIN" "$DIST_DIR/"

# 4. macOS: bundle the remaining dynamic libraries
if [ "$IS_MAC" = true ]; then
    target_bin="$DIST_DIR/$BINARY_NAME"

    echo "==> Bundling dynamic libraries"
    otool -L "$target_bin" | awk 'NR>1 {print $1}' | while IFS= read -r dep; do
        case "$dep" in
            /usr/lib/*|/System/*|@executable_path/*)
                continue
                ;;
        esac
        lib="$(basename "$dep")"
        echo "    $lib"
        cp -f "$dep" "$DIST_DIR/$lib"
        chmod u+w "$DIST_DIR/$lib"
        install_name_tool -change "$dep" "@executable_path/$lib" "$target_bin"
        # Make sure the bundled library has a valid (ad hoc) signature
        codesign --force --sign - "$DIST_DIR/$lib"
    done

    echo "==> Signing $BINARY_NAME"
    codesign --force --sign - "$target_bin"

    echo "==> Verifying the bundle"
    if otool -L "$target_bin" | tail -n +2 | awk '{print $1}' \
         | grep -Eq '^(/opt/|/usr/local/|/Users/)'; then
        echo "ERROR: the binary still references a non-system library:"
        otool -L "$target_bin"
        exit 1
    fi
    if ! codesign --verify "$target_bin" >/dev/null 2>&1; then
        echo "ERROR: the code signature is not valid."
        exit 1
    fi
fi

echo "==> Binary layout"
if command -v file >/dev/null 2>&1; then
    file "$DIST_DIR/$BINARY_NAME"
else
    ls -lh "$DIST_DIR/$BINARY_NAME"
fi

if [ "$IS_MAC" = true ] && command -v otool >/dev/null 2>&1; then
    minos="$(otool -l "$DIST_DIR/$BINARY_NAME" \
        | awk '/LC_BUILD_VERSION/{found=1} found && /minos/{print $2; exit}')"
    echo "==> Minimum macOS version: ${minos:-unknown} (requested: $MACOSX_DEPLOYMENT_TARGET)"
    if [ -n "$minos" ] && awk -v a="$minos" -v b="$MACOSX_DEPLOYMENT_TARGET" \
         'BEGIN { exit !(a + 0 > b + 0) }'; then
        echo "WARNING: the binary requires macOS $minos although" >&2
        echo "         MACOSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET was requested." >&2
        echo "         A linked library was built for a newer macOS. Rebuild the" >&2
        echo "         Homebrew dependencies with the same target, for example:" >&2
        echo "           MACOSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET brew install --build-from-source ..." >&2
    fi
fi

# 5. Smoke test with the bundled binary
echo "==> Running smoke tests"
test_dir="$base_dir/source/testing"
if [ ! -f "$test_dir/input.tif" ]; then
    echo "ERROR: $test_dir/input.tif not found."
    echo "       The smoke test needs the complete source/ tree."
    exit 1
fi
cp -f "$DIST_DIR"/* "$test_dir"/
rm -f "$test_dir/output.tif" "$test_dir/output_nondefault.tif"

(
    cd "$test_dir"
    "./$BINARY_NAME" -i input.tif -o output.tif -odir ./
)

if [ ! -s "$test_dir/output.tif" ]; then
    echo "ERROR: the test run did not create output.tif."
    exit 1
fi

(
    cd "$test_dir"
    "./$BINARY_NAME" -i input.tif -o output_nondefault.tif -odir ./ \
        -graphfg 4 -internal 0
)

if [ ! -s "$test_dir/output_nondefault.tif" ]; then
    echo "ERROR: the test run with non-default parameters failed."
    exit 1
fi

echo "==> Smoke tests passed"
echo "    $test_dir/output.tif"
echo "    $test_dir/output_nondefault.tif"

# 5b. Compare the result with the reference output (pixel values only)
if [ "${MSPA_SKIP_VERIFY:-0}" = "1" ]; then
    echo "==> Pixel comparison skipped (MSPA_SKIP_VERIFY=1)"
else
    echo "==> Building pixel comparison tool"
    tiff_inc=""
    tiff_lib=""
    if [ "$IS_MAC" = true ]; then
        tiff_inc="-I$BREW_PREFIX/include"
        tiff_lib="-L$BREW_PREFIX/lib"
    fi
    "$LIB_CC" -std=c99 -O2 -Wall $tiff_inc -o "$TIFFCMP" \
        source/testing/tiffcmp.c $tiff_lib -ltiff -lm

    if [ -f source/testing/reference.tif ] && [ -f source/testing/reference_nondefault.tif ]; then
        echo "==> Verifying output.tif against source/testing/reference.tif"
        "$TIFFCMP" source/testing/reference.tif source/testing/output.tif
        echo "==> Verifying output_nondefault.tif against source/testing/reference_nondefault.tif"
        "$TIFFCMP" source/testing/reference_nondefault.tif source/testing/output_nondefault.tif
    else
        echo "WARNING: reference images not found in source/testing/; skipping pixel comparison."
    fi
fi

# 6. Remove intermediate build files
echo "==> Cleaning up intermediate files"
make -C source/mial-master/core/c clean
make -C source -f "$MAKEFILE" clean
rm -f source/libmiallib_mspa.a
rm -f "$TIFFCMP"

echo
echo "==> Done: $DIST_DIR/"
ls -lh "$DIST_DIR"
if [ "$IS_MAC" = true ]; then
    echo
    echo "Distribute the whole directory $DIST_DIR/ (not only the executable)."
fi
