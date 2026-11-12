#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Build, bundle and smoke-test the standalone MSPA binary.
#
# Usage: ./deploy.sh <target>
#
# The script builds the static miallib library, compiles the MSPA driver,
# creates a self-contained distribution directory and runs a test image
# through the freshly built binary.
# ---------------------------------------------------------------------------
set -euo pipefail

if [ -z "${BASH_VERSION:-}" ]; then
    echo "ERROR: this script must be run with bash (e.g. ./deploy.sh Linux)."
    exit 1
fi

trap 'echo "ERROR: build failed at line $LINENO." >&2' ERR

base_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$base_dir"

usage() {
    cat <<'EOF'
Usage: ./deploy.sh <target>

Targets:
  Linux         Linux x86_64 (glibc based, e.g. Debian/Ubuntu)
  Linux_ARM     Linux aarch64 / ARM64
  macOS_ARM     macOS on Apple Silicon (Homebrew GCC)
  Windows       MS-Windows x86_64 (MSYS2 UCRT64)
  Windows_ARM   MS-Windows ARM64 (MSYS2 CLANGARM64)
EOF
}

target="${1:-}"
if [ -z "$target" ]; then
    usage
    exit 1
fi

MAKEFILE=""
BINARY_NAME=""
DIST_DIR=""
IS_MAC=false
LIB_CC="${MSPA_CC:-gcc}"
EXTRA_CFLAGS=""

case "$target" in
    "Linux")
        MAKEFILE="Makefile_Linux";        BINARY_NAME="mspa_linux";         DIST_DIR="mspa_dist_linux"
        ;;
    "Linux_ARM")
        MAKEFILE="Makefile_Linux_ARM";    BINARY_NAME="mspa_linux_ARM";     DIST_DIR="mspa_dist_linux_ARM"
        ;;
    "macOS_ARM")
        MAKEFILE="Makefile_macOS_ARM";    BINARY_NAME="mspa_macOS_ARM";     DIST_DIR="mspa_dist_macOS_ARM"
        IS_MAC=true; LIB_CC="${MSPA_CC:-gcc-16}"
        ;;
    "Windows")
        MAKEFILE="Makefile_Windows";      BINARY_NAME="mspa_win64.exe";     DIST_DIR="mspa_dist_windows"
        ;;
    "Windows_ARM")
        MAKEFILE="Makefile_Windows_ARM";  BINARY_NAME="mspa_win64_ARM.exe"; DIST_DIR="mspa_dist_windows_ARM"
        ;;
    *)
        echo "Unknown target: $target"
        usage
        exit 1
        ;;
esac

if [ ! -f "$MAKEFILE" ]; then
    echo "ERROR: $MAKEFILE not found in $base_dir"
    exit 1
fi

# Pixel comparison tool (no Python/ImageMagick dependency, uses libtiff)
case "$target" in
    "Windows"|"Windows_ARM") TIFFCMP="testing/tiffcmp.exe" ;;
    *)                       TIFFCMP="testing/tiffcmp" ;;
esac

# Number of parallel compile jobs (portable across Linux, macOS and MSYS2)
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
case "$jobs" in
    ''|*[!0-9]*) jobs=4 ;;
esac

echo "==> Target:          $target"
echo "==> Base directory:  $base_dir"
echo "==> Distribution:    $DIST_DIR/"

if [ "$IS_MAC" = true ]; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "ERROR: Homebrew not found. Install it from https://brew.sh"
        exit 1
    fi
    BREW_PREFIX="$(brew --prefix)"
    EXTRA_CFLAGS="-I$BREW_PREFIX/include"
    if ! command -v "$LIB_CC" >/dev/null 2>&1; then
        echo "ERROR: compiler $LIB_CC not found. Run: brew install gcc"
        exit 1
    fi
    echo "==> Homebrew:        $BREW_PREFIX"
fi

if ! command -v "$LIB_CC" >/dev/null 2>&1; then
    echo "ERROR: compiler $LIB_CC not found."
    exit 1
fi

# 1. Build the static miallib library and copy it next to this script
echo "==> Building libmiallib_mspa.a with $LIB_CC"
make -C mial-master/core/c clean
make -C mial-master/core/c -j"$jobs" \
     CC="$LIB_CC" EXTRA_CFLAGS="$EXTRA_CFLAGS" \
     ../build/lib/libmiallib_mspa.a
if [ ! -f mial-master/core/build/lib/libmiallib_mspa.a ]; then
    echo "ERROR: libmiallib_mspa.a was not created."
    exit 1
fi
cp -f mial-master/core/build/lib/libmiallib_mspa.a .

# 2. Compile the driver
echo "==> Compiling $BINARY_NAME with $MAKEFILE"
make -f "$MAKEFILE" clean
make -f "$MAKEFILE" CC="$LIB_CC"

if [ ! -f "$BINARY_NAME" ]; then
    echo "ERROR: $BINARY_NAME was not created."
    exit 1
fi

# 3. Create the distribution directory
echo "==> Creating $DIST_DIR/"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
cp -f "$BINARY_NAME" "$DIST_DIR/"

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

# 5. Smoke test with the bundled binary
echo "==> Running smoke tests"
test_dir="$base_dir/testing"
mkdir -p "$test_dir"
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
        testing/tiffcmp.c $tiff_lib -ltiff -lm

    if [ -f testing/reference.tif ] && [ -f testing/reference_nondefault.tif ]; then
        echo "==> Verifying output.tif against testing/reference.tif"
        "$TIFFCMP" testing/reference.tif testing/output.tif
        echo "==> Verifying output_nondefault.tif against testing/reference_nondefault.tif"
        "$TIFFCMP" testing/reference_nondefault.tif testing/output_nondefault.tif
    else
        echo "WARNING: reference images not found in testing/; skipping pixel comparison."
    fi
fi

# 6. Remove intermediate build files
echo "==> Cleaning up intermediate files"
make -C mial-master/core/c clean
make -f "$MAKEFILE" clean
rm -f libmiallib_mspa.a
rm -f "$TIFFCMP"

echo
echo "==> Done: $DIST_DIR/"
ls -lh "$DIST_DIR"
if [ "$IS_MAC" = true ]; then
    echo
    echo "Distribute the whole directory $DIST_DIR/ (not only the executable)."
fi
