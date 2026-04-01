idf_build_get_property(project_ver PROJECT_VER)
idf_build_get_property(idf_target IDF_TARGET)

set(BSL_SIGNATURE_PRODUCT_NAME "BSL-HTLS Gen2")
set(BSL_SIGNATURE_PROJECT_NAME "bsl_laser_controller")
set(BSL_SIGNATURE_PROTOCOL_VERSION "host-v1")
set(BSL_SIGNATURE_HARDWARE_SCOPE "main-controller")
set(BSL_SIGNATURE_FIRMWARE_VERSION "${project_ver}")
set(BSL_SIGNATURE_BOARD_NAME "${idf_target}")
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
        "src/laser_controller_comms.c"
        "src/laser_controller_config.c"
        "src/laser_controller_faults.c"
        "src/laser_controller_logger.c"
        "src/laser_controller_safety.c"
        "src/laser_controller_service.c"
        "src/laser_controller_state.c"
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
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
