# You may install cmake from https://cmake.org/download/
cmake_minimum_required(VERSION 3.22.0)
project("video-player-example")

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
