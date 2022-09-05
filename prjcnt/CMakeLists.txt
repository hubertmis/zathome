# Find Zephyr. This also loads Zephyr's build system.
cmake_minimum_required(VERSION 3.13.1)

set(CONF_FILE "prj.conf")
list(APPEND CONF_FILE "../config/openthread.conf")
list(APPEND CONF_FILE "../config/coap.conf")
list(APPEND CONF_FILE "../config/fota.conf")
list(APPEND CONF_FILE "../config/dfu.conf")

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rgbw)

# Add your source file to the "app" target. This must come after
# find_package(Zephyr) which defines the target.
target_sources(app PRIVATE src/coap.c)
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/notification.c)
target_sources(app PRIVATE src/prov.c)
target_sources(app PRIVATE src/pwr_det.c)

# TODO: Replace with library target
target_include_directories(app PRIVATE ../lib)
target_sources(app PRIVATE ../lib/cbor_utils.c)
target_sources(app PRIVATE ../lib/coap_fota.c)
target_sources(app PRIVATE ../lib/coap_sd.c)
target_sources(app PRIVATE ../lib/coap_server.c)
target_sources(app PRIVATE ../lib/continuous_sd.c)

if(COAPS_PSK)
    target_compile_definitions(app PRIVATE
        -DCOAPS_PSK=${COAPS_PSK}
        )
endif()
