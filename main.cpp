#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <iostream>
#include <string>
#include <memory>
#include <functional>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class BinanceWebSocketClient : public std::enable_shared_from_this<BinanceWebSocketClient>
{
private:
    net::io_context& ioc_;
    ssl::context ctx_;
    tcp::resolver resolver_;
    websocket::stream<beast::ssl_stream<tcp::socket>> ws_;
    beast::flat_buffer buffer_;
    
    std::string host_;
    std::string port_;
    std::string target_;

    // Callbacks
    std::function<void()> on_connect_;
    std::function<void(const std::string&)> on_message_;
    std::function<void(const std::string&)> on_error_;

public:
    BinanceWebSocketClient(net::io_context& ioc, const std::string& host, 
                          const std::string& port, const std::string& target)
        : ioc_(ioc)
        , ctx_(ssl::context::tlsv12_client)
        , resolver_(ioc)
        , ws_(ioc, ctx_)
        , host_(host)
        , port_(port)
        , target_(target)
    {
        // Set up SSL context
        ctx_.set_default_verify_paths();
    }

    // Set callback functions
    void set_on_connect(std::function<void()> callback) {
        on_connect_ = std::move(callback);
    }

    void set_on_message(std::function<void(const std::string&)> callback) {
        on_message_ = std::move(callback);
    }

    void set_on_error(std::function<void(const std::string&)> callback) {
        on_error_ = std::move(callback);
    }

    // Start the connection process
    void connect() {
        // Start by resolving the hostname
        resolver_.async_resolve(host_, port_,
            [self = shared_from_this()](beast::error_code ec, tcp::resolver::results_type results) {
                if (ec) {
                    self->handle_error("Resolve failed: " + ec.message());
                    return;
                }
                // Use the free function async_connect directly in the lambda
                net::async_connect(beast::get_lowest_layer(self->ws_), results,
                    [self](beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
                        if (ec) {
                            self->handle_error("Connect failed: " + ec.message());
                            return;
                        }
                        self->on_connect_tcp(ep);
                    });
            });
    }

private:

    void on_connect_tcp(tcp::resolver::results_type::endpoint_type ep) {
        // Set SSL verification
        ws_.next_layer().set_verify_mode(ssl::verify_peer);
        ws_.next_layer().set_verify_callback(
            [](bool preverified, ssl::verify_context& ctx) {
                // For this example, we'll accept any certificate
                // In production, you should verify properly
                return true;
            });

        // Update host for SNI
        std::string host = host_ + ':' + std::to_string(ep.port());

        // Perform SSL handshake
        ws_.next_layer().async_handshake(ssl::stream_base::client,
            [self = shared_from_this(), host](beast::error_code ec) {
                if (ec) {
                    self->handle_error("SSL handshake failed: " + ec.message());
                    return;
                }
                self->on_ssl_handshake(host);
            });
    }

    void on_ssl_handshake(const std::string& host) {
        // Set WebSocket options
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(http::field::user_agent, "BinanceWSClient/1.0");
            }));

        // Perform WebSocket handshake
        ws_.async_handshake(host, target_,
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) {
                    self->handle_error("WebSocket handshake failed: " + ec.message());
                    return;
                }
                self->on_websocket_handshake();
            });
    }

    void on_websocket_handshake() {
        // Connection successful - notify callback
        if (on_connect_) {
            on_connect_();
        }

        // Start reading messages
        start_read();
    }

    void start_read() {
        ws_.async_read(buffer_,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) {
                    self->handle_error("Read failed: " + ec.message());
                    return;
                }
                self->on_message_received(bytes_transferred);
            });
    }

    void on_message_received(std::size_t bytes_transferred) {
        // Convert buffer to string
        std::string message = beast::buffers_to_string(buffer_.data());
        
        // Clear the buffer
        buffer_.consume(bytes_transferred);

        // Call message callback
        if (on_message_) {
            on_message_(message);
        }

        // Continue reading
        start_read();
    }

    void handle_error(const std::string& error_msg) {
        if (on_error_) {
            on_error_(error_msg);
        }
    }

public:
    // Gracefully close the WebSocket connection
    void close() {
        ws_.async_close(websocket::close_code::normal,
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) {
                    self->handle_error("Close failed: " + ec.message());
                }
            });
    }
};

int main()
{
    try
    {
        // Create io_context
        net::io_context ioc;

        // Create WebSocket client
        auto client = std::make_shared<BinanceWebSocketClient>(
            ioc, 
            "stream.binance.com", 
            "9443", 
            "/stream?streams=btcusdt@bookTicker"
        );

        // Set up callbacks
        client->set_on_connect([]() {
            std::cout << "Connected to Binance WebSocket!" << std::endl;
            std::cout << "Listening for BTC/USDT book ticker data..." << std::endl;
        });

        client->set_on_message([](const std::string& message) {
            std::cout << message << std::endl;
        });

        client->set_on_error([](const std::string& error) {
            std::cerr << error << std::endl;
        });

        // Start the connection
        std::cout << "Connecting to Binance WebSocket..." << std::endl;
        client->connect();

        // Run the io_context - this will block and process all async operations
        std::cout << "Starting event loop..." << std::endl;
        ioc.run();

        std::cout << "Event loop stopped." << std::endl;
    }
    catch(std::exception const& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// CMakeLists.txt example:
/*
cmake_minimum_required(VERSION 3.10)
project(BinanceWebSocketAsync)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

add_executable(binance_ws_async main.cpp)

target_link_libraries(binance_ws_async 
    Boost::system 
    OpenSSL::SSL 
    OpenSSL::Crypto
    pthread
)
*/