#include <iostream>
#include <string>
#include <algorithm>
#include <optional>
#include "rainwaysdk.h"

// Mirrors rainway::RainwayLogLevel indicies for conversion to string
const char *LOG_LEVEL_STR_MAP[] = {"Silent", "Error", "Warning", "Info", "Debug", "Trace"}; 

// host-example entry point
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

    auto hr = rainway::Initialize();
    if (hr != rainway::Error::RAINWAY_ERROR_SUCCESS) {
        std::cout << "Error. Failed to initialize Rainway: " << hr << std::endl;
        return 1;
    }

    // install the global logging handlers
    rainway::SetLogLevel(rainway::LogLevel::RAINWAY_LOG_LEVEL_INFO, nullptr);
    rainway::SetLogSink([](rainway::LogLevel level, const char* target, const char* message) {
         std::cout << LOG_LEVEL_STR_MAP[level] << " [" << target << "] " << message << std::endl;
    });

    // set up the connection config
    rainway::Connection::CreateOptions config;
    config.apiKey = apiKey;
    config.externalId = "rainway-sdk-native-host-example";

    // try to create a connection
    rainway::Connection::Create(config, rainway::Connection::CreatedCallback{
        // on success
        [](rainway::Connection conn) {
            // log information about the SDK
            std::cout << "Connected to the Rainway Network as Peer " << conn.Id() << " using SDK version " << rainway::internal::rainway_version() << std::endl;

            // set up the peer handler
            conn.SetPeerConnectionRequestHandler(rainway::Connection::PeerConnectionRequestHandler{
                [](rainway::IncomingConnectionRequest req) {
                    // accept all requests, handling the created peer
                    req.Accept(rainway::PeerOptions{}, rainway::IncomingConnectionRequest::AcceptCallback{
                        // on success
                        [](rainway::PeerConnection peer) {
                            // set a state change handler for the peer to log when it changes state
                            peer.SetStateChangeHandler(rainway::PeerConnection::StateChangeHandler {
                                [&](rainway::PeerConnection::State state) {
                                    std::cout << "Peer " << peer.Id() << " moved to state " << state << std::endl;
                                }
                            });

                            // accept all stream requests, handling the created stream 
                            peer.SetOutboundStreamRequestHandler(rainway::PeerConnection::OutboundStreamRequestHandler {
                                [](rainway::OutboundStreamRequest req) {
                                    // for this demo, we always create a full desktop, all permission stream
                                    // for your application, you probably want something better scoped than this
                                    rainway::OutboundStreamStartOptions config;
                                    config.type = rainway::StreamType::RAINWAY_STREAM_TYPE_FULL_DESKTOP;
                                    config.defaultPermissions = rainway::InputLevel::RAINWAY_INPUT_LEVEL_ALL;

                                    // accept the stream
                                    req.Accept(config, rainway::OutboundStreamStartCallback {
                                        [](rainway::OutboundStream stream) {
                                            std::cout << "Stream " << stream.Id() << " created" << std::endl;
                                        }
                                    });
                                }
                            });

                            // monitor data channel creation, installing an echo handler on each one
                            peer.SetDataChannelOpenedHandler(rainway::PeerConnection::DataChannelOpenedHandler {
                                [](rainway::DataChannel channel) {
                                    std::cout << "Channel " << channel.name << " created" << std::endl;

                                    // install the handler
                                    channel.SetDataChannelDataHandler(rainway::DataChannel::DataChannelDataHandler {
                                        [&](rainway::DataChannel::DataChannelDataEvent ev) {
                                            std::cout << "Got message" << std::endl;
                                            // wrap the bit message in a vector
                                            std::vector<uint8_t> input(ev.data, ev.data + (ev.len * sizeof(const uint8_t)));

                                            // reverse the vector in place
                                            std::reverse(input.begin(), input.end());

                                            // send the peer the reversed vector message
                                            auto hr = channel.Send(input);
                                            if (hr != rainway::Error::RAINWAY_ERROR_SUCCESS) {
                                                std::cout << "Failed to send data to channel " << channel.name << " due to: " << hr << std::endl;
                                            }
                                        }
                                    });
                                }
                            });
                        },
                        // on failure
                        [](rainway::Error err) {
                             std::cout << "Error. Failed to accept connection: " << err << std::endl;
                        }
                    });
                },
            });
        },
        // on failure
        [](rainway::Error err) {
            // log
            std::cout << "Error. Failed to connect to Rainway: " << err << std::endl;
        }
    });

    std::cout << "Connecting to Rainway..." << std::endl;

    std::cout << "Press any key to exit." << std::endl;

    // wait for user input, if received, shutdown
    std::cin.get();

    std::cout << "Input received. Shutting down Rainway..." << std::endl;

    rainway::Shutdown();

    return 0;
}
