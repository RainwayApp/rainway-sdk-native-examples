#include <iostream>
#include <string>
#include <algorithm>
#include <optional>
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
    uint16_t minPort = 0;
    uint16_t maxPort = 0;

    if (argc >= 3)
        minPort = (uint16_t)std::strtol(argv[2], nullptr, 10);

    if (argc >= 4)
        maxPort = (uint16_t)std::strtol(argv[3], nullptr, 10);

    std::cout << "Using minPort: " << minPort << " and maxPort: " << maxPort << std::endl;

    if (minPort > maxPort)
    {
        std::cout << "Error. minPort must be less than maxPort." << std::endl;
        abort();
    }

    if (minPort == 0 && maxPort == 0)
    {
        std::cout << "Note: Since port limits are both zero, ports will not be limited." << std::endl;
    }

    auto config = rainway::Config{
        // The Rainway API Key to authentication with
        apiKey,
        // The Rainway External Id to identify ourselves as
        "rainway-sdk-native-echo-example",
        // The min host port to use (zero is default)
        minPort,
        // The max host port to use (zero disables limiting the port)
        maxPort,
        // Optional callback for when connection to the Gateway is lost
        std::nullopt,
        // Optional callback for when a connection request is received from a peer
        [](const rainway::Runtime &runtime, const rainway::ConnectionRequest req)
        {
            // accept all connections
            req.accept();
        },
        // Optional callback for when a peer's connection state changes.
        [](const rainway::Runtime &runtime, const rainway::Peer &peer, rainway::RainwayPeerState state)
        {
            std::cout << "Peer " << peer.peerId() << " moved to state " << state << std::endl;
        },
        // Optional callback for when a peer message has been received
        [](const rainway::Runtime &runtime, const rainway::Peer &peer, std::string channel, const uint8_t *msg, size_t msg_size)
        {
            // wrap the bit message in a vector
            std::vector<uint8_t> input(msg, msg + (msg_size * sizeof(const uint8_t)));

            // reverse the vector in place
            std::reverse(input.begin(), input.end());

            // send the peer the reversed vector message
            peer.send(channel, input);
        },
        // Optional callback for when a peer data channel is created
        std::nullopt,
        // Optional callback for when an error has been received from a peer
        std::nullopt,
        // Optional callback for when a stream request has been received
        [](const rainway::Runtime &runtime, const rainway::StreamRequest req)
        {
            // accept all stream requests, granting full input permissions
            req.accept(rainway::RainwayStreamConfig{
                (rainway::RainwayInputLevel)ALL_INPUT_LEVEL,
                nullptr,
            });
        },
        // Optional callback for when a stream announcement has been received
        std::nullopt,
        // Optional callback for when a stream has been stopped
        std::nullopt};

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
