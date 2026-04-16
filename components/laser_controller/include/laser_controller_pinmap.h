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

/*
 * External-GPIO aliases on the J2 Sensor & LED connector.
 *
 *   GPIO6 (EXT_SHARED_0): ToF daughterboard LED-driver CTRL input. Driven
 *   by the firmware via a dual-writer sideband discipline (service bring-up
 *   + runtime). This ownership is unchanged by the 2026-04-15 button-board
 *   addition — see AGENT.md "GPIO6 LED (TPS61169) — Two Control Paths".
 *
 *   GPIO7 (EXT_SHARED_1): MCP23017 INTA input (open-drain, active-low). The
 *   button-board MCP23017 (@0x20) asserts INTA whenever any of the four
 *   configured button pins (GPA0..GPA3) change state. ESP32 side uses an
 *   internal pull-up to hold the line high when the MCP is quiescent.
 *
 *   Historically (pre-2026-04-15) this pin was the VL53L1X GPIO1 data-ready
 *   input (`LASER_CONTROLLER_GPIO_TOF_GPIO1_INT`). That assignment was
 *   dropped because the physical daughterboard connector is shared with the
 *   new button board and only one interrupt source can own the line. The
 *   ToF now runs in polling-only mode — range_status register (RESULT__RANGE_STATUS)
 *   is read on the normal slow-cycle cadence.
 */
#define LASER_CONTROLLER_GPIO_TOF_LED_CTRL           LASER_CONTROLLER_GPIO_EXT_SHARED_0
#define LASER_CONTROLLER_GPIO_BUTTON_INTA            LASER_CONTROLLER_GPIO_EXT_SHARED_1

/*
 * I2C addresses on the shared GPIO4/5 bus. Every active device on the bus
 * MUST have a unique 7-bit address; collisions cause silent corruption.
 *
 *   0x20  MCP23017  button-board GPIO expander (added 2026-04-15)
 *   0x28  STUSB4500 USB-PD sink PHY
 *   0x29  VL53L1X   ToF range sensor (daughterboard)
 *   0x48  DAC80502  dual-channel DAC
 *   0x5A  DRV2605   haptic driver (shared 0x5A, not 0x5B)
 *   0x60  TLC59116  button-board RGB status LED driver (added 2026-04-15)
 *   0x68  TLC59116  ALLCALL default — passive; reserved for the above chip
 */
#define LASER_CONTROLLER_BUTTON_MCP23017_I2C_ADDR    0x20U
#define LASER_CONTROLLER_BUTTON_TLC59116_I2C_ADDR    0x60U

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
