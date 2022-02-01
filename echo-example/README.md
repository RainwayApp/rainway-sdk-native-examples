# echo-example

This example demonstrates how to build a simple echo server using the Rainway SDK in a C++ application.

For more information about Rainway, see [our docs](https://docs.rainway.com). To sign up, visit [Rainway.com](https://rainway.com).

## Getting Started

> Requires [CMake `>=3.22.0`](https://cmake.org/download/) and [a supported Generator](https://cmake.org/cmake/help/v3.22/manual/cmake-generators.7.html) (e.g. [Visual Studio](https://visualstudio.microsoft.com/vs/)).

```
# Generate the build system
cmake . -B build

# Run the build
cmake --build build

# Run the built application with your Rainway API Key as the first and only argument
../build/bin/Debug/echo-example.exe <your_rainway_api_key>
```

### Building Release

The above steps create a `Debug` binary by default. To build a release binary, instruct `cmake` to do so:

```
# Generate the build system
cmake . -DCMAKE_BUILD_TYPE=Release -B build

# Run the build
cmake --build build

# Run the built application with your Rainway API Key as the first and only argument
../build/bin/Release/echo-example.exe <your_rainway_api_key>
```

For more information about build configuration, see [The CMAKE Docs](https://cmake.org/cmake/help/v3.22/index.html).
