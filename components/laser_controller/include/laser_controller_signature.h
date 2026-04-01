#pragma once

#include <stdint.h>

#define LASER_CONTROLLER_FIRMWARE_SIGNATURE_MAGIC        "BSLFWS1"
#define LASER_CONTROLLER_FIRMWARE_SIGNATURE_END_MAGIC    "BSLEND1"
#define LASER_CONTROLLER_FIRMWARE_SIGNATURE_SCHEMA_V1    1U
#define LASER_CONTROLLER_FIRMWARE_PRODUCT_NAME           "BSL-HTLS Gen2"
#define LASER_CONTROLLER_FIRMWARE_PROJECT_NAME           "bsl_laser_controller"
#define LASER_CONTROLLER_FIRMWARE_PROTOCOL_VERSION       "host-v1"
#define LASER_CONTROLLER_FIRMWARE_HARDWARE_SCOPE         "main-controller"

typedef struct __attribute__((packed)) {
    char magic[8];
    uint16_t schema_version;
    uint16_t struct_size;
    char product_name[24];
    char project_name[32];
    char board_name[16];
    char protocol_version[16];
    char hardware_scope[24];
    char firmware_version[32];
    char build_utc[24];
    char payload_sha256_hex[65];
    char end_magic[8];
    uint8_t reserved[3];
} laser_controller_firmware_signature_t;

const laser_controller_firmware_signature_t *laser_controller_signature_get(void);
