#!/bin/bash
# Exit immediately if a command exits with a non-zero status
set -e

# Identify and echo the base directory where this script resides
base_dir="$(cd "$(dirname "${BASH_SOURCE}")" && pwd)"
echo "📍 Base directory: $base_dir"

# Usage help function
print_usage() {
    echo "❌ Error: Invalid or missing target."
    echo "📋 Usage: ./deploy.sh [Linux | Linux_ARM | macOS_Intel | macOS_ARM | Windows | Windows_ARM ]"
    exit 1
}

# Ensure a target argument was provided
if [ -z "$1" ]; then
    print_usage
fi

TARGET_OS="$1"

# Dynamically set variables based on selected OS target
case "$TARGET_OS" in
    "macOS_ARM")
        MAKEFILE="Makefile_macOS_ARM"
        BINARY_NAME="mspa_macOS_ARM"
        DIST_DIR="mspa_dist_arm"
        IS_MAC=true
        RUNNABLE=true
        ;;
    "macOS_Intel")
        MAKEFILE="Makefile_macOS_Intel"
        BINARY_NAME="mspa_macOS_Intel"
        DIST_DIR="mspa_dist_intel"
        IS_MAC=true
        RUNNABLE=true
        ;;
    "Linux")
        MAKEFILE="Makefile_Linux"
        BINARY_NAME="mspa_linux"
        DIST_DIR="mspa_dist_linux"
        IS_MAC=false
        RUNNABLE=true
        ;;
    "Linux_ARM")
        MAKEFILE="Makefile_Linux_ARM"
        BINARY_NAME="mspa_linux_ARM"
        DIST_DIR="mspa_dist_linux_ARM"
        IS_MAC=false
        RUNNABLE=true
        ;;
    "Windows")
        MAKEFILE="Makefile_Windows"
        BINARY_NAME="mspa_win64.exe"
        DIST_DIR="mspa_dist_windows"
        IS_MAC=false
        RUNNABLE=true
        ;;
    "Windows_ARM")
        MAKEFILE="Makefile_Windows_ARM"
        BINARY_NAME="mspa_win64_ARM.exe"
        DIST_DIR="mspa_dist_windows_ARM"
        IS_MAC=false
        RUNNABLE=true
        ;;
    *)
        print_usage
        ;;
esac

echo "🚀 Starting build and deployment process for target: $TARGET_OS..."

# 1. Build the static library and copy it to the base directory
echo "📦 Building static library: libmiallib_mspa.a..."
cd mial-master/core/c
make clean
make ../build/lib/libmiallib_mspa.a
cp ../build/lib/libmiallib_mspa.a "$base_dir"

# Navigate back to the base directory for the main compilation
cd "$base_dir"

# 2. Clean previous target artifacts using the specific Makefile
echo "🧹 Cleaning previous build..."
make -f "$MAKEFILE" clean

# 3. Compile the binary using the specific Makefile
echo "🏗️ Compiling $BINARY_NAME using $MAKEFILE..."
make -f "$MAKEFILE"

# 4. Create a clean distribution directory
echo "📦 Creating distribution directory: $DIST_DIR/"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# 5. Copy the compiled executable to the distribution directory
cp "$BINARY_NAME" "$DIST_DIR/"

# 6. Run macOS-specific linking, signing, and verification steps
if [ "$IS_MAC" = true ]; then
    echo "📂 Copying shared libraries for macOS..."
    # Ensure correct Homebrew paths (Handles both ARM /opt/homebrew and Intel /usr/local paths)
    BREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/opt/homebrew")
    
    cp "$BREW_PREFIX/opt/libdeflate/lib/libdeflate.0.dylib" "$DIST_DIR/"
    cp "$BREW_PREFIX/opt/liblerc/lib/libLerc.4.dylib"       "$DIST_DIR/"
    cp "$BREW_PREFIX/opt/gsl/lib/libgsl.28.dylib"           "$DIST_DIR/"
    cp "$BREW_PREFIX/opt/gsl/lib/libgslcblas.0.dylib"       "$DIST_DIR/"

    echo "🔗 Isolating library dependency paths to @executable_path..."
    TARGET_BIN="$DIST_DIR/$BINARY_NAME"

    install_name_tool -change "$BREW_PREFIX/opt/libdeflate/lib/libdeflate.0.dylib" @executable_path/libdeflate.0.dylib "$TARGET_BIN"
    install_name_tool -change "$BREW_PREFIX/opt/liblerc/lib/libLerc.4.dylib"       @executable_path/libLerc.4.dylib       "$TARGET_BIN"
    install_name_tool -change "$BREW_PREFIX/opt/gsl/lib/libgsl.28.dylib"           @executable_path/libgsl.28.dylib           "$TARGET_BIN"
    install_name_tool -change "$BREW_PREFIX/opt/gsl/lib/libgslcblas.0.dylib"       @executable_path/libgslcblas.0.dylib       "$TARGET_BIN"

    echo "✍️ Applying ad-hoc code signature..."
    codesign --force --deep --sign - "$TARGET_BIN"

    # Verification Phase (macOS only)
    echo -e "\n🔍 Running Verification Tests..."
    echo "-----------------------------------------------"
    echo "1. Checking dynamic linkage layout:"
    otool -L "$TARGET_BIN"
    echo "-----------------------------------------------"
    echo "2. Validating binary architectural layout:"
    file "$TARGET_BIN"
    echo "-----------------------------------------------"
    echo "3. Testing code signature state:"
    if codesign --verify --verbose "$TARGET_BIN" 2>&1 | grep -q "valid on disk"; then
        echo "✅ Code signature is VALID on disk."
    else
        codesign --verify --verbose "$TARGET_BIN"
    fi
    echo "-----------------------------------------------"
else
    # Non-macOS instructions (Linux / Windows)
    echo "ℹ️ Skipping macOS post-processing steps (otool, install_name_tool, codesign) for target $TARGET_OS."
    echo "-----------------------------------------------"
    echo "🔍 Validating binary architectural layout:"
    file "$DIST_DIR/$BINARY_NAME"
    echo "-----------------------------------------------"
fi

# 7. Test Run Phase
echo -e "\n🧪 Initializing Test Run Phase..."
TEST_DIR="$base_dir/testing"

if [ "$RUNNABLE" = true ]; then
    # Ensure testing directory exists
    if [ ! -d "$TEST_DIR" ]; then
        echo "⚠️ Testing directory layout not found at $TEST_DIR. Creating it..."
        mkdir -p "$TEST_DIR"
    fi

    # Wipe stale outputs and break existing permission locks on previous .dylib files
    rm -f "$TEST_DIR/output.tif"
    rm -f "$TEST_DIR"/*.dylib
    rm -f $TEST_DIR/mspa*

    # a) Copy all compiled files and isolated libraries from the bundle into the testing environment
    echo "📋 Copying runtime artifacts into testing directory..."
    cp -R "$DIST_DIR"/. "$TEST_DIR/"

    # b) Navigate to test bed and execute binary locally
    echo "🏃 Executing test execution command inside $TEST_DIR..."
    cd "$TEST_DIR"
    
    # Prepend './' for POSIX binary execution security checks
    ./"$BINARY_NAME" -i input.tif -o output.tif -odir ./

    # c) Verify generation proof
    if [ -f "output.tif" ]; then
        echo "-----------------------------------------------"
        echo "🎉 Test run was successful! Output file 'output.tif' was generated on disk."
        echo "-----------------------------------------------"
    else
        echo "❌ Error: The test execution completed, but 'output.tif' was not generated."
        exit 1
    fi

    # d) Reset environment back to base directory
    cd "$base_dir"
else
    echo "⏩ Skipping execution run. Cannot natively run '$BINARY_NAME' binaries directly on this host system."
    echo "-----------------------------------------------"
fi


# 8. Final Workspace Cleanup
echo "🧹 Performing final post-build workspace cleanup..."
cd mial-master/core/c
make clean

cd "$base_dir"
make -f "$MAKEFILE" clean
rm -f libmiallib_mspa.a

echo -e "\n✅ Success! Your fully portable bundle is ready in the '$DIST_DIR' folder."
echo "📋 Structure of your deployment package:"
ls -lh "$DIST_DIR"
