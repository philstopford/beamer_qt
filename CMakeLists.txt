cmake_minimum_required(VERSION 4.3)
project(beamer_qt)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(beamer_qt main.cpp device.cpp media_server.cpp media_preparer.cpp bonjour_discovery.cpp)
target_link_libraries(beamer_qt PRIVATE Qt6::Widgets Qt6::Network)

# Prefer the platform DNS-SD daemon so discovery benefits from its interface
# handling and cache. macOS supplies this API in libSystem. Linux commonly
# supplies it through Avahi's compatibility library; Bonjour SDK installs can
# supply the same API on Windows. The Rust mDNS browser remains the fallback.
if(APPLE)
    target_compile_definitions(beamer_qt PRIVATE BEAMER_HAVE_SYSTEM_DNSSD)
elseif(UNIX OR WIN32)
    find_path(DNSSD_INCLUDE_DIR dns_sd.h PATH_SUFFIXES avahi-compat-libdns_sd)
    find_library(DNSSD_LIBRARY NAMES dns_sd dnssd)
    if(DNSSD_INCLUDE_DIR AND DNSSD_LIBRARY)
        target_compile_definitions(beamer_qt PRIVATE BEAMER_HAVE_SYSTEM_DNSSD)
        target_include_directories(beamer_qt PRIVATE "${DNSSD_INCLUDE_DIR}")
        target_link_libraries(beamer_qt PRIVATE "${DNSSD_LIBRARY}")
        message(STATUS "System DNS-SD discovery enabled: ${DNSSD_LIBRARY}")
    else()
        message(STATUS "System DNS-SD library not found; using portable mDNS discovery")
    endif()
endif()

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(test_device tests/test_device.cpp device.cpp media_server.cpp media_preparer.cpp)
target_link_libraries(test_device PRIVATE Qt6::Network Qt6::Test)
add_test(NAME device_parser COMMAND test_device)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/backends")
configure_file(backends/pyatv_bridge.py "${CMAKE_CURRENT_BINARY_DIR}/backends/pyatv_bridge.py" COPYONLY)

find_program(CARGO_EXECUTABLE cargo)
find_program(RUSTC_EXECUTABLE rustc)
if(CARGO_EXECUTABLE)
    get_filename_component(CARGO_BIN_DIR "${CARGO_EXECUTABLE}" DIRECTORY)
    set(CARGO_ENV_CMD ${CMAKE_COMMAND} -E env)
    if(RUSTC_EXECUTABLE)
        list(APPEND CARGO_ENV_CMD "RUSTC=${RUSTC_EXECUTABLE}")
    endif()
    if(CARGO_BIN_DIR)
        if(WIN32)
            list(APPEND CARGO_ENV_CMD "PATH=${CARGO_BIN_DIR};$ENV{PATH}")
        else()
            list(APPEND CARGO_ENV_CMD "PATH=${CARGO_BIN_DIR}:$ENV{PATH}")
        endif()
    endif()

    if(WIN32)
        add_custom_target(native_backends ALL
            COMMAND ${CARGO_ENV_CMD} ${CARGO_EXECUTABLE} build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chromecast/Cargo.toml" --bin beamer-chromecast --offline
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chromecast/target/debug/beamer-chromecast${CMAKE_EXECUTABLE_SUFFIX}"
                "${CMAKE_CURRENT_BINARY_DIR}/backends/beamer-chromecast${CMAKE_EXECUTABLE_SUFFIX}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Building native Chromecast backend"
        )
    else()
        add_custom_target(native_backends ALL
            COMMAND ${CARGO_ENV_CMD} ${CARGO_EXECUTABLE} build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/third_party/airfry/rust/Cargo.toml" --bin airfry --offline
        COMMAND ${CARGO_ENV_CMD} ${CARGO_EXECUTABLE} build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chromecast/Cargo.toml" --bin beamer-chromecast --offline
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/airfry/rust/target/debug/airfry${CMAKE_EXECUTABLE_SUFFIX}"
                "${CMAKE_CURRENT_BINARY_DIR}/backends/airfry${CMAKE_EXECUTABLE_SUFFIX}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chromecast/target/debug/beamer-chromecast${CMAKE_EXECUTABLE_SUFFIX}"
                "${CMAKE_CURRENT_BINARY_DIR}/backends/beamer-chromecast${CMAKE_EXECUTABLE_SUFFIX}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Building native AirPlay and Chromecast backends"
        )
    endif()
    add_dependencies(beamer_qt native_backends)
    if(WIN32)
        add_custom_command(TARGET beamer_qt POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:beamer_qt>/backends"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_BINARY_DIR}/backends/beamer-chromecast${CMAKE_EXECUTABLE_SUFFIX}"
                "$<TARGET_FILE_DIR:beamer_qt>/backends/beamer-chromecast${CMAKE_EXECUTABLE_SUFFIX}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/backends/pyatv_bridge.py"
                "$<TARGET_FILE_DIR:beamer_qt>/backends/pyatv_bridge.py"
            COMMENT "Packaging Windows helper backends"
        )
    endif()
endif()

if(APPLE)
    set_target_properties(beamer_qt PROPERTIES
        WIN32_EXECUTABLE FALSE
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Beamer Qt"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.beamer.beamer-qt"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
else()
    set_target_properties(beamer_qt PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
