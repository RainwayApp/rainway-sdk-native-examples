# host-example

This example demonstrates how to build a simple host application using the Rainway SDK in a C++ application.

It accepts all incoming stream requests from clients (such as the [Web Demo](https://webdemo.rainway.com/)).

For more information about Rainway, see [our docs](https://docs.rainway.com). To sign up, visit [rainway.com](https://rainway.com).

## Building and running this example

See the [parent README](../README.md) for detailed build instructions.

```ps1
cd ..
cmake . -B build
cmake --build build -t host-example

# Get your Rainway API key here: https://hub.rainway.com/keys
.\build\bin\Debug\host-example.exe pk_live_YourRainwayApiKey
```
