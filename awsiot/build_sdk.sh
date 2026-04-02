#!/bin/sh



# Ensure all submodules are properly updated
git submodule update --init --recursive

# Make a build directory for the SDK. Can use any name.
mkdir aws-iot-device-sdk-cpp-v2-build
cd aws-iot-device-sdk-cpp-v2-build

# Generate the SDK build files.
# -DCMAKE_INSTALL_PREFIX needs to be the absolute/full path to the directory.
#     (Example: "/Users/example/sdk-workspace/).
# -DCMAKE_BUILD_TYPE can be "Release", "RelWithDebInfo", or "Debug"
#cmake -DCMAKE_INSTALL_PREFIX="$(BASE)" -DCMAKE_BUILD_TYPE="Debug" ../aws-iot-device-sdk-cpp-v2

#build shared libs
cmake -DCMAKE_INSTALL_PREFIX="$PWD/.." -DCMAKE_BUILD_TYPE="Debug" -DBUILD_SHARED_LIBS="ON" -DUSE_OPENSSL=ON ../aws-iot-device-sdk-cpp-v2

# Build and install the library. Once installed, you can develop with the SDK and run the samples
#cmake --build . --target install

