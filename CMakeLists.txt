cmake_minimum_required(VERSION 4.3)
project(beamer_qt)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(beamer_qt main.cpp device.cpp media_server.cpp)
target_link_libraries(beamer_qt PRIVATE Qt6::Widgets Qt6::Network)

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(test_device tests/test_device.cpp device.cpp media_server.cpp)
target_link_libraries(test_device PRIVATE Qt6::Network Qt6::Test)
add_test(NAME device_parser COMMAND test_device)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/backends")
configure_file(backends/pyatv_bridge.py "${CMAKE_CURRENT_BINARY_DIR}/backends/pyatv_bridge.py" COPYONLY)

add_custom_target(native_backends ALL
    COMMAND cargo build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/third_party/airfry/rust/Cargo.toml" --bin airfry --offline
    COMMAND cargo build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chromecast/Cargo.toml" --bin beamer-chromecast --offline
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/airfry/rust/target/debug/airfry"
        "${CMAKE_CURRENT_BINARY_DIR}/backends/airfry"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chromecast/target/debug/beamer-chromecast"
        "${CMAKE_CURRENT_BINARY_DIR}/backends/beamer-chromecast"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building native AirPlay and Chromecast backends"
)

set_target_properties(beamer_qt PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
