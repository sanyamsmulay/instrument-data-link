# Build Tools

This directory contains the build system implementation for the Instrument Panel project.

## Files

- `Containerfile`: Defines the build environment with all required dependencies
- `container-build.sh`: Internal script that handles the actual build process for different architectures

## Usage

From the project root directory, run:

```bash
# Make the build script executable
chmod +x build.sh

# Build for all architectures
./build.sh

# Or build for a specific architecture
./build.sh x86_64  # For x86_64
./build.sh arm64   # For ARM64
./build.sh armhf   # For ARM32
```

## Build Outputs

The build artifacts will be placed in the `build/` directory in the project root, organized by architecture:

```
build/
  ├── x86_64/
  │   └── instrument-panel-x86_64
  ├── arm64/
  │   └── instrument-panel-arm64
  └── armhf/
      └── instrument-panel-armhf
```

## Requirements

- Podman installed on the host system
- Internet connection (for first build to download base image and packages)
