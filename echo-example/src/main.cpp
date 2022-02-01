#include <iostream>
#include <string>
#include <algorithm>
#include "rainwaysdk.h"

// constant bitmask value for granting all input permissions
const auto ALL_INPUT_LEVEL = rainway::RainwayInputLevel::Mouse |
                             rainway::RainwayInputLevel::Keyboard |
                             rainway::RainwayInputLevel::Gamepad;

// Mirrors rainway::RainwayLogLevel indicies for conversion to string
const char *LOG_LEVEL_STR_MAP[] = {"Silent", "Error", "Warning", "Info", "Debug", "Trace"};

// echo-example entry point
// expects your API_KEY as the first and only argument
int main(int argc, char *argv[])
{
    // check we have api key
    if (argc < 2)
    {
        std::cout << "Error. Expected your Rainway API key as an argument. Exiting." << std::endl;
        return 1;
    }

    auto apiKey = argv[1];

    auto config = rainway::Config{
        // The Rainway API Key to authentication with
        apiKey,
        // The Rainway External Id to identify ourselves as
        "rainway-sdk-native-echo-example",
        // The min host port to use (zero is default)
        0,
        // The max host port to use (zero disables limiting the port)
        0,
        // Optional callback for when connection to Instant Relay is lost
        nullptr,
        // Optional callback for when a connection request is received from a peer
        [](const rainway::ConnectionRequest req)
        {
            // accept all connections
            req.accept();
        },
        // Optional callback for when a new peer has connected
        nullptr,
        // Optional callback for when a peer has disconnected
        nullptr,
        // Optional callback for when a peer message has been received
        [](const rainway::Peer &peer, std::string channel, const uint8_t *msg, size_t msg_size)
        {
            // wrap the bit message in a vector
            std::vector<uint8_t> input(msg[0], msg[msg_size]);

            // reverse the vector in place
            std::reverse(input.begin(), input.end());

            // send the peer the reversed vector message
            peer.send(channel, input);
        },
        // Optional callback for when a peer data channel is created
        nullptr,
        // Optional callback for when an error has been received from a peer
        nullptr,
        // Optional callback for when a stream request has been received
        [](const rainway::StreamRequest req)
        {
            // accept all stream requests, granting full input permissions
            req.accept(rainway::RainwayStreamConfig{
                (rainway::RainwayInputLevel)ALL_INPUT_LEVEL,
                nullptr,
            });
        },
        // Optional callback for when a stream announcement has been received
        nullptr,
        // Optional callback for when a stream has been stopped
        nullptr};

    std::cout << "Connecting to Rainway..." << std::endl;

    // setup the Rainway SDK static logging before we actually initialize the SDK
    // this ensures we are able to see all logs that are emitted on level Error, Warning, or Info
    rainway::Runtime::setLogLevel(rainway::RainwayLogLevel::Info, nullptr);
    rainway::Runtime::setLogSink([](rainway::RainwayLogLevel level, const char *target, const char *msg)
                                 { std::cout << LOG_LEVEL_STR_MAP[level] << " [" << target << "] " << msg << std::endl; });

    // initialize the runtime, waiting for the async work, and retrieve the result as 'runtime'
    auto runtime = std::get<0>(rainway::Runtime::initialize(config).get());

    // log information about the SDK
    std::cout << "Connected to the Rainway Network as Peer " << runtime->peerId() << " using SDK version " << runtime->prettyVersion() << std::endl;

    std::cout << "Press any key to exit.";

    // wait for user input, if received, shutdown
    std::cin.get();

    std::cout << "Input received. Shutting down Rainway..." << std::endl;

    // explicitly drop the runtime ptr, cleaning up the rainway sdk internals
    runtime.reset();

    std::cout << "Shut down. Exiting..." << std::endl;

    return 0;
}
