<div align="center">

![A multi-colored spiral on a dark background with the Rainway logo and the text "Native Examples" in the center](.github/header.jpg)

[💯 Stable Branch](https://github.com/RainwayApp/rainway-sdk-native-examples/tree/main) - [🚀 Beta Branch](https://github.com/RainwayApp/rainway-sdk-native-examples/tree/beta) - [📘 Read the Docs](https://docs.rainway.com/docs/native-getting-started) - [🎁 Get Started](https://hub.rainway.com/landing)

[![CI/CD](https://github.com/RainwayApp/rainway-sdk-native-examples/actions/workflows/ci-cd.yml/badge.svg)](https://github.com/RainwayApp/rainway-sdk-native-examples/actions/workflows/ci-cd.yml)

</div>

# rainway-sdk-native-examples

Various examples of how to use the Rainway SDK with C++:

- [host-example](./host-example/) - A basic example that allows all web clients to connect and stream the desktop.
- [video-player-example](./video-player-example/) - A [BYOFB mode](https://docs.rainway.com/docs/byofb) example that streams a video file to all web clients.

For more information about using Rainway, see [our docs](https://docs.rainway.com). To sign up, visit [Rainway.com](https://rainway.com).

## Getting Started

Get [CMake `>=3.22.0`](https://cmake.org/download/) and [a supported Generator](https://cmake.org/cmake/help/v3.22/manual/cmake-generators.7.html) (e.g. [Visual Studio](https://visualstudio.microsoft.com/vs/)).

To build all examples:

```sh
# Generate the build system for all examples
cmake . -B build

# Run the builds using the generated system
cmake --build build
```

See `README.md` within each example for further instructions.
