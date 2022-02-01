# You may install cmake from https://cmake.org/download/
cmake_minimum_required(VERSION 3.22.0)
project("echo-example")
include(FetchContent)

# The version of the Rainway SDK to use
set(RAINWAY_SDK_VERSION "0.2.3")

# The md5 hash of the Rainway SDK zip for this version
set(RAINWAY_SDK_MD5_HASH "BE920F8C7C798A3AD7D97E1DAEFB50B2")

message("Attempting to download Rainway SDK v${RAINWAY_SDK_VERSION} with md5 hash '${RAINWAY_SDK_MD5_HASH}'")

# Download the sdk
FetchContent_Declare(
    rainwaysdk
    URL https://sdk-builds.rainway.com/cpp/${RAINWAY_SDK_VERSION}.zip
    URL_HASH MD5=${RAINWAY_SDK_MD5_HASH}
)

# Make the SDK available for use
FetchContent_MakeAvailable(rainwaysdk)

# Create an executable target from our source
add_executable(${PROJECT_NAME} src/main.cpp)

# Specify the target uses the c++ linker
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Specify the target binary should output to <build_dir>/bin
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Specify the target uses c++17
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# Link the target against the downloaded rainwaysdk
target_link_libraries(${PROJECT_NAME} rainwaysdk)

# Include the downloaded rainwaysdk include dir (where the header is) for the target
# Note: rainwaysdk_SOURCE_DIR is autocreated by FetchContent_MakeAvailable()
target_include_directories(${PROJECT_NAME} PRIVATE ${rainwaysdk_SOURCE_DIR}/include)

# Include the downloaded rainwaysdk root dir (where the dll and lib are) for the target
# Note: rainwaysdk_SOURCE_DIR is autocreated by FetchContent_MakeAvailable()
target_link_directories(${PROJECT_NAME} PRIVATE ${rainwaysdk_SOURCE_DIR})

# Add a custom command to copy the rainwaysdk dll to the build directory
# If the build directory has a different version (or no dll)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${rainwaysdk_SOURCE_DIR}/rainwaysdk.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>)
