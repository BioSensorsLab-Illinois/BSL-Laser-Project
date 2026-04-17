idf_build_get_property(project_ver PROJECT_VER)
idf_build_get_property(idf_target IDF_TARGET)

set(BSL_SIGNATURE_PRODUCT_NAME "BSL-HTLS Gen2")
set(BSL_SIGNATURE_PROJECT_NAME "bsl_laser_controller")
set(BSL_SIGNATURE_PROTOCOL_VERSION "host-v1")
set(BSL_SIGNATURE_HARDWARE_SCOPE "main-controller")
set(BSL_SIGNATURE_FIRMWARE_VERSION "${project_ver}")
set(BSL_SIGNATURE_BOARD_NAME "${idf_target}")
# 2026-04-16: capture compile time at build, not configure time, so the
# host-console can actually tell which binary is on the device.
# `string(TIMESTAMP …)` only fires at CMake configure — incremental
# `idf.py build`s would bake the same stale timestamp forever, which
# misled triage by making the firmware look older than it was.
# Convention: -fmacro-prefix-map and __DATE__/__TIME__ are translation-unit
# specific, so we still seed the configure-time string here as a fallback,
# then the autogen .c.in file gets its own update by re-configuring whenever
# any tracked source changes (added below via add_custom_command).
string(TIMESTAMP BSL_SIGNATURE_BUILD_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
set(
    BSL_SIGNATURE_PAYLOAD
    "schema=1|product=${BSL_SIGNATURE_PRODUCT_NAME}|project=${BSL_SIGNATURE_PROJECT_NAME}|board=${BSL_SIGNATURE_BOARD_NAME}|protocol=${BSL_SIGNATURE_PROTOCOL_VERSION}|scope=${BSL_SIGNATURE_HARDWARE_SCOPE}|fw=${BSL_SIGNATURE_FIRMWARE_VERSION}|build=${BSL_SIGNATURE_BUILD_UTC}"
)
string(SHA256 BSL_SIGNATURE_PAYLOAD_SHA256 "${BSL_SIGNATURE_PAYLOAD}")
set(BSL_SIGNATURE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/laser_controller_signature_autogen.c")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/src/laser_controller_signature_autogen.c.in"
    "${BSL_SIGNATURE_SOURCE}"
    @ONLY
)

idf_component_register(
    SRCS
        "src/laser_controller_app.c"
        "src/laser_controller_bench.c"
        "src/laser_controller_board.c"
        "src/laser_controller_buttons.c"
        "src/laser_controller_rgb_led.c"
        "src/laser_controller_comms.c"
        "src/laser_controller_config.c"
        "src/laser_controller_deployment.c"
        "src/laser_controller_faults.c"
        "src/laser_controller_logger.c"
        "src/laser_controller_safety.c"
        "src/laser_controller_service.c"
        "src/laser_controller_state.c"
        "src/laser_controller_usb_debug_mock.c"
        "src/laser_controller_wireless.c"
        "${BSL_SIGNATURE_SOURCE}"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_timer
        freertos
        esp_app_format
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_driver_ledc
        esp_adc
        esp_driver_usb_serial_jtag
        esp_event
        esp_http_server
        esp_netif
        esp_wifi
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# Force the firmware signature autogen .c to recompile on every build so
# the embedded __DATE__ __TIME__ macros capture the actual compile time.
# Without this, ninja's incremental build skips the recompile because the
# source content didn't change, leaving build_utc stale across edits.
# (2026-04-16 — context: triage was misled by a 12-hour-stale build_utc.)
add_custom_target(bsl_signature_touch ALL
    COMMAND ${CMAKE_COMMAND} -E touch_nocreate "${BSL_SIGNATURE_SOURCE}"
    DEPENDS "${BSL_SIGNATURE_SOURCE}"
    COMMENT "Refresh BSL firmware signature compile-time timestamp"
)
add_dependencies(${COMPONENT_LIB} bsl_signature_touch)
