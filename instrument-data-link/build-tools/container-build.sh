#!/bin/bash
set -e

# Function to build for specific architecture
build_for_arch() {
    local arch=$1
    echo "Building for architecture: $arch"
    
    # Set up architecture-specific compiler and flags
    case $arch in
        "arm64")
            export CXX=aarch64-linux-gnu-g++
            export CFLAGS="-march=armv8-a"
            export CXXFLAGS="-march=armv8-a"
            export LIB_PATH="/usr/lib/aarch64-linux-gnu"
            export BOOST_LIBS="-lboost_system -lboost_thread -lboost_regex -lboost_date_time"
            ;;
        "armhf")
            export CXX=arm-linux-gnueabihf-g++
            export CFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"
            export CXXFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -std=c++17"
            export LIB_PATH="/usr/lib/arm-linux-gnueabihf"
            export BOOST_LIBS="-lboost_system -lboost_thread -lboost_regex -lboost_date_time"
            ;;
        "x86_64")
            export CXX=g++
            export CFLAGS="-march=x86-64"
            export CXXFLAGS="-march=x86-64 -std=c++17"
            export LIB_PATH="/usr/lib/x86_64-linux-gnu"
            export BOOST_LIBS="-lboost_system -lboost_thread -lboost_regex -lboost_date_time"
            ;;
        "win64")
            export CXX=x86_64-w64-mingw32-g++-posix
            export CFLAGS="-m64 -march=x86-64"
            export CXXFLAGS="-m64 -march=x86-64 -std=c++17 -static-libgcc -static-libstdc++ -pthread -D_WIN32 -DWIN32 -D_WINDOWS -I/usr/x86_64-w64-mingw32/include"
            export LIB_PATH="/usr/x86_64-w64-mingw32/lib"
            export BOOST_LIBS="-lboost_system -lboost_thread -lboost_regex -lboost_date_time -lws2_32 -lmswsock -lbcrypt"
            ;;
        *)
            echo "Unsupported architecture: $arch"
            exit 1
            ;;
    esac

    # Create build directory
    mkdir -p /build/output/$arch
    cd /build/output/$arch

    # Source files
    SRC_SOURCES=""
    for file in /build/src/*.cpp; do
        if [ -f "$file" ]; then
            SRC_SOURCES+="$file "
        fi
    done

    # Collect source files from different components
    
    # Headers directory sources
    HEADERS_SOURCES=""
    for file in /build/src/headers/*.cpp; do
        if [ -f "$file" ]; then
            HEADERS_SOURCES+="$file "
        fi
    done

    # JetBridge sources
    JETBRIDGE_SOURCES=""
    for file in /build/src/jetbridge/*.cpp; do
        if [ -f "$file" ]; then
            JETBRIDGE_SOURCES+="$file "
        fi
    done

    # Redist sources
    REDIST_SOURCES=""
    for file in /build/src/redist/*.cpp; do
        if [ -f "$file" ]; then
            REDIST_SOURCES+="$file "
        fi
    done

    # vJoy SDK sources
    VJOY_SOURCES=""
    for file in /build/src/vJoy_SDK/*.cpp; do
        if [ -f "$file" ]; then
            VJOY_SOURCES+="$file "
        fi
    done

    # Build the project
    $CXX $CXXFLAGS -o instrument-data-link-$arch \
        -I /build/src \
        -I /build/src/headers \
        -I /build/src/jetbridge \
        -I /build/src/redist \
        -I /build/src/vJoy_SDK \
        $SRC_SOURCES \
        $HEADERS_SOURCES \
        $JETBRIDGE_SOURCES \
        $REDIST_SOURCES \
        $VJOY_SOURCES \
        -L$LIB_PATH \
        $BOOST_LIBS -lssl -lcrypto -lz -lpthread

    echo "Build completed for $arch"
    cd /build
}

# Build for the specified architecture or all supported ones
if [ -z "$TARGET_ARCH" ] || [ "$TARGET_ARCH" = "all" ]; then
    build_for_arch "x86_64"
    build_for_arch "arm64"
    build_for_arch "armhf"
    build_for_arch "win64"
else
    build_for_arch "$TARGET_ARCH"
fi
