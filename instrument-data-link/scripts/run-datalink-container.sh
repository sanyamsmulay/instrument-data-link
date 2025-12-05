#!/bin/bash

# Exit on error
set -e -x

# Container configuration
BUILDER_IMAGE="instrument-data-link-builder"
RUNTIME_IMAGE="datalink-x86"
CONTAINER_NAME="datalink-container"
BUILD_TOOLS_DIR="$(dirname "$(realpath "$0")")/../../instrument-data-link/build-tools"

# Build the container if it doesn't exist
# Check if binary needs to be built
BINARY_PATH="$(dirname "$BUILD_TOOLS_DIR")/../build/x86_64/instrument-data-link-x86_64"
if [ ! -f "$BINARY_PATH" ]; then
    echo "Building x86_64 binary..."
    $BUILD_TOOLS_DIR/build.sh x86_64
fi

# Build runtime image if it doesn't exist
if ! podman image exists $RUNTIME_IMAGE; then
    echo "Creating runtime image..."
    cd "$(dirname "$BUILD_TOOLS_DIR")" && podman build -t $RUNTIME_IMAGE -f build-tools/containerfile_runtime .
fi

# Check if container exists and remove it
if podman container exists $CONTAINER_NAME; then
    echo "Removing existing container..."
    podman rm -f $CONTAINER_NAME
fi

# Run the container with host networking
echo "Starting container..."
podman run -d \
    --name $CONTAINER_NAME \
    -p 52020:52020 \
    -p 52021:52021 \
    -p 52022:52022 \
    --network host \
    $RUNTIME_IMAGE

echo "Container started successfully!"
echo "Port mappings:"
echo "- 52020: Instrument Panel Listen Port"
echo "- 52021: Data Link Instrument Listen Port"
echo "- 52022: Data Link Simulator Listen Port"

# Follow container logs
podman logs -f $CONTAINER_NAME