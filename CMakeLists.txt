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