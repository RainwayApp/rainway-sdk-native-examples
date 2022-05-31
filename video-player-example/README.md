# video-player-example

This C++ example uses the Rainway SDK's [BYOFB mode](https://docs.rainway.com/docs/byofb) and [MediaFoundation](https://docs.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk) to stream video from a file.

It accepts all incoming stream requests from clients (such as the [Web Demo](https://webdemo.rainway.com/)), and streams the video to them. Try connecting with multiple clients at once!

For more information about Rainway, see [our docs](https://docs.rainway.com). To sign up, visit [rainway.com](https://rainway.com).

## Building and running this example

See the [parent README](../README.md) for detailed build instructions.

```ps1
cd ..
cmake . -B build
cmake --build build -t video-player-example

# Get your Rainway API key here: https://hub.rainway.com/keys
.\build\bin\Debug\video-player-example.exe pk_live_YourRainwayApiKey C:\path\to\media.mp4
```
