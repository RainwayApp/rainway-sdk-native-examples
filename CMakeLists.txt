# You may install cmake from https://cmake.org/download/
cmake_minimum_required(VERSION 3.22.0)
project("rainway-sdk-native-examples")

include(FetchContent)

# The version of the Rainway SDK to use
set(RAINWAY_SDK_VERSION "0.3.0-beta.1")

# The md5 hash of the Rainway SDK zip for this version
# From powershell you can use `Get-FileHash -Algorithm MD5 -Path <path>`
set(RAINWAY_SDK_MD5_HASH "55172A800FC0F80C53BC0E9A2BB339E4")

message("Attempting to download Rainway SDK v${RAINWAY_SDK_VERSION} with md5 hash '${RAINWAY_SDK_MD5_HASH}'")

# Download the sdk
FetchContent_Declare(
    rainwaysdk
    URL https://sdk-builds.rainway.com/cpp/${RAINWAY_SDK_VERSION}.zip
    URL_HASH MD5=${RAINWAY_SDK_MD5_HASH}
)

# Make the SDK available for use
FetchContent_MakeAvailable(rainwaysdk)

# Add our example subdirectories
add_subdirectory("host-example")
add_subdirectory("video-player-example")
