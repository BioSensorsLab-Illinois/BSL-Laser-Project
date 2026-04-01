#pragma once

/*
 * Cross-validated from:
 * - docs/Schematics/MainPCB.NET and MainPCB.pdf
 * - docs/Schematics/BMS-Board.NET and BMS.pdf
 * - docs/Schematics/USB_PD-PHY.NET and USB-PD.pdf
 * - Espressif ESP32-S3-WROOM-1 / WROOM-1U datasheet Table 3-1
 *
 * Safety caveats from the current hardware set:
 * - GPIO37 drives both ERM_TRIG and the visible laser load-switch enable net.
 * - GPIO4/GPIO5 are the default shared I2C bus for STUSB4500, DAC80502,
 *   DRV2605, and the external Sensor & LED board connector.
 * - GPIO6/GPIO7 leave the main board and are shared with both the Sensor & LED
 *   board connector and the BMS battery-toggle connector.
 * - GPIO35/GPIO36/GPIO37 are only available on compatible module variants.
 *   Do not substitute an Octal-PSRAM ESP32-S3 module variant without first
 *   re-validating these nets.
 * - GPIO47/GPIO48 are assumed to be 3.3 V logic in this design. Do not
 *   substitute a 1.8 V-only module variant without redesign and review.
 */

/* Native USB and UART debug */
#define LASER_CONTROLLER_GPIO_USB_D_N                19
#define LASER_CONTROLLER_GPIO_USB_D_P                20
#define LASER_CONTROLLER_GPIO_UART0_RX               44
#define LASER_CONTROLLER_GPIO_UART0_TX               43

/* Rail enables and rail-good inputs */
#define LASER_CONTROLLER_GPIO_PWR_TEC_EN             15
#define LASER_CONTROLLER_GPIO_PWR_TEC_PGOOD          16
#define LASER_CONTROLLER_GPIO_PWR_LD_EN              17
#define LASER_CONTROLLER_GPIO_PWR_LD_PGOOD           18

/* Laser driver interface */
#define LASER_CONTROLLER_GPIO_LD_SBDN                13
#define LASER_CONTROLLER_GPIO_LD_PCN                 21
#define LASER_CONTROLLER_GPIO_LD_LPGD                14
#define LASER_CONTROLLER_GPIO_LD_CURRENT_MONITOR     2
#define LASER_CONTROLLER_GPIO_LD_DRIVER_TEMP_MONITOR 1

/* TEC controller interface */
#define LASER_CONTROLLER_GPIO_TEC_TMO                8
#define LASER_CONTROLLER_GPIO_TEC_ITEC               9
#define LASER_CONTROLLER_GPIO_TEC_VTEC               10
#define LASER_CONTROLLER_GPIO_TEC_TEMPGD             47

/* IMU SPI interface */
#define LASER_CONTROLLER_GPIO_IMU_SDI                38
#define LASER_CONTROLLER_GPIO_IMU_CS                 39
#define LASER_CONTROLLER_GPIO_IMU_SCLK               40
#define LASER_CONTROLLER_GPIO_IMU_SDO                41
#define LASER_CONTROLLER_GPIO_IMU_INT2               42

/* DRV2605 and visible alignment laser */
#define LASER_CONTROLLER_GPIO_ERM_EN                 48
#define LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN      37

/* Default shared I2C bus stuffing */
#define LASER_CONTROLLER_GPIO_SHARED_I2C_SDA         4
#define LASER_CONTROLLER_GPIO_SHARED_I2C_SCL         5

/* Alternate I2C stuffing options shown as DNP in the current netlist */
#define LASER_CONTROLLER_GPIO_ALT_DAC_I2C_SDA        11
#define LASER_CONTROLLER_GPIO_ALT_DAC_I2C_SCL        12
#define LASER_CONTROLLER_GPIO_ALT_ERM_I2C_SDA        35
#define LASER_CONTROLLER_GPIO_ALT_ERM_I2C_SCL        36

/* Exported external-purpose GPIO */
#define LASER_CONTROLLER_GPIO_EXT_SHARED_0           6
#define LASER_CONTROLLER_GPIO_EXT_SHARED_1           7

/* Boot straps and reserved test points */
#define LASER_CONTROLLER_GPIO_BOOT_BUTTON            0
#define LASER_CONTROLLER_GPIO_BOOT_OPTION            46
#define LASER_CONTROLLER_GPIO_JTAG_STRAP_OPEN        3
#define LASER_CONTROLLER_GPIO_VDD_SPI_STRAP_OPEN     45

/* Default board stuffing from the uploaded netlists */
#define LASER_CONTROLLER_BOARD_STUFFING_DAC_ON_SHARED_I2C     1
#define LASER_CONTROLLER_BOARD_STUFFING_ERM_ON_SHARED_I2C     1
#define LASER_CONTROLLER_BOARD_STUFFING_DAC_ON_ALT_I2C        0
#define LASER_CONTROLLER_BOARD_STUFFING_ERM_ON_ALT_I2C        0
#define LASER_CONTROLLER_BOARD_VISIBLE_LASER_FROM_TEC_5V      1
#define LASER_CONTROLLER_BOARD_VISIBLE_LASER_FROM_DVDD_3V3    0
#define LASER_CONTROLLER_BOARD_DAC_POWERS_UP_ZERO_SCALE       1

