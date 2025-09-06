#!/bin/bash
set -e

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." &> /dev/null && pwd)"
cd "$SCRIPT_DIR"

# Build the container image
echo "Building container image..."
podman build -t instrument-data-link-builder -f Containerfile .

# Function to build for a specific architecture
build_arch() {
    local arch=$1
    echo "Building for $arch..."
    
    # Create output directory
    mkdir -p "$PROJECT_ROOT/build/$arch"
    
    # Run the build in container
    # Copy everything to a temp directory
    TEMP_DIR=$(mktemp -d)
    cp -r "$PROJECT_ROOT/src"/* "$TEMP_DIR/"
    cp -r "$PROJECT_ROOT/headers" "$TEMP_DIR/"
    cp -r "$PROJECT_ROOT/jetbridge" "$TEMP_DIR/"

    # Run the build in container
    podman run --rm \
        -v "$TEMP_DIR:/build/src:ro" \
        -v "$PROJECT_ROOT/build/$arch:/build/output/$arch" \
        -e TARGET_ARCH=$arch \
        instrument-data-link-builder
}

# Build for all architectures or specific one
if [ "$1" = "" ] || [ "$1" = "all" ]; then
    build_arch "x86_64"
    build_arch "arm64"
    build_arch "armhf"
    build_arch "win64"
else
    build_arch "$1"
fi

echo "Build complete! Check the build/* directories for outputs."
