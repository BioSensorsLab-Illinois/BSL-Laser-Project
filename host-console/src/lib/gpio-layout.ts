import type { GpioInspectorStatus, GpioPinReadback } from '../types'

export type GpioModuleSide = 'left' | 'right'
export type GpioModuleSignalClass =
  | 'bus'
  | 'power'
  | 'analog'
  | 'control'
  | 'strap'
  | 'usb'
  | 'debug'

export type GpioAnalogTelemetry =
  | 'tecTempAdc'
  | 'ldCurrentMonitor'
  | 'ldDriverTemp'

export type GpioModulePinMeta = {
  gpioNum: number
  modulePin: number
  side: GpioModuleSide
  signalClass: GpioModuleSignalClass
  netName: string
  label: string
  detail: string
  analogTelemetry?: GpioAnalogTelemetry
  riskNote?: string
}

export const gpioModulePins: GpioModulePinMeta[] = [
  { gpioNum: 4, modulePin: 4, side: 'left', signalClass: 'bus', netName: 'GPIO4', label: 'Shared I2C SDA', detail: 'Shared I2C SDA for DAC, DRV2605, STUSB4500, and ToF board.' },
  { gpioNum: 5, modulePin: 5, side: 'left', signalClass: 'bus', netName: 'GPIO5', label: 'Shared I2C SCL', detail: 'Shared I2C SCL for DAC, DRV2605, STUSB4500, and ToF board.' },
  { gpioNum: 6, modulePin: 6, side: 'left', signalClass: 'control', netName: 'GPIO6', label: 'ToF LED / ext', detail: 'Shared external GPIO used by the ToF LED-control sideband and BMS connector.' },
  { gpioNum: 7, modulePin: 7, side: 'left', signalClass: 'control', netName: 'GPIO7', label: 'ToF INT / ext', detail: 'Shared external GPIO used by the ToF interrupt sideband and BMS connector.' },
  { gpioNum: 15, modulePin: 8, side: 'left', signalClass: 'power', netName: 'PWR_TEC_EN', label: 'TEC rail EN', detail: 'Service and runtime enable for the TEC MPM3530 rail.' },
  { gpioNum: 16, modulePin: 9, side: 'left', signalClass: 'power', netName: 'PWR_TEC_PGOOD', label: 'TEC PGOOD', detail: 'Power-good input from the TEC MPM3530 rail.' },
  { gpioNum: 17, modulePin: 10, side: 'left', signalClass: 'power', netName: 'PWR_LD_EN', label: 'LD rail EN', detail: 'Service and runtime enable for the laser-driver MPM3530 rail.' },
  { gpioNum: 18, modulePin: 11, side: 'left', signalClass: 'power', netName: 'PWR_LD_PGOOD', label: 'LD PGOOD', detail: 'Power-good input from the laser-driver MPM3530 rail.' },
  { gpioNum: 8, modulePin: 12, side: 'left', signalClass: 'analog', netName: 'TEC_TMO', label: 'TEC temp monitor', detail: 'Analog TEC temperature proxy input.', analogTelemetry: 'tecTempAdc' },
  { gpioNum: 19, modulePin: 13, side: 'left', signalClass: 'usb', netName: 'ESP_D_N', label: 'USB D-', detail: 'Native USB D- for Web Serial and flashing.', riskNote: 'Overriding native USB pins can drop the host link immediately.' },
  { gpioNum: 20, modulePin: 14, side: 'left', signalClass: 'usb', netName: 'ESP_D_P', label: 'USB D+', detail: 'Native USB D+ for Web Serial and flashing.', riskNote: 'Overriding native USB pins can drop the host link immediately.' },
  { gpioNum: 3, modulePin: 15, side: 'left', signalClass: 'strap', netName: 'GPIO_3', label: 'Reserved test pin', detail: 'Reserved/open test point only.', riskNote: 'Reserved strap/test pin; do not use in production logic.' },
  { gpioNum: 46, modulePin: 16, side: 'left', signalClass: 'strap', netName: 'ESP_GPIO46', label: 'Boot option strap', detail: 'Strapping input used for boot behavior.', riskNote: 'Changing strap pins can affect boot and download-mode behavior.' },
  { gpioNum: 9, modulePin: 17, side: 'left', signalClass: 'analog', netName: 'TEC_ITEC', label: 'TEC current', detail: 'Analog TEC current telemetry input.' },
  { gpioNum: 10, modulePin: 18, side: 'left', signalClass: 'analog', netName: 'TEC_VTEC', label: 'TEC voltage', detail: 'Analog TEC voltage telemetry input.' },
  { gpioNum: 11, modulePin: 19, side: 'left', signalClass: 'bus', netName: 'GPIO11', label: 'Alt DAC SDA', detail: 'DNP alternate DAC I2C SDA route.' },
  { gpioNum: 12, modulePin: 20, side: 'left', signalClass: 'bus', netName: 'GPIO12', label: 'Alt DAC SCL', detail: 'DNP alternate DAC I2C SCL route.' },
  { gpioNum: 13, modulePin: 21, side: 'right', signalClass: 'control', netName: 'LD_SBDN', label: 'Laser standby', detail: 'Fast beam-off / standby control into the ATLS6A214 driver.' },
  { gpioNum: 14, modulePin: 22, side: 'right', signalClass: 'control', netName: 'LD_LPGD', label: 'Driver loop good', detail: 'Loop-good status input from the laser driver.' },
  { gpioNum: 21, modulePin: 23, side: 'right', signalClass: 'control', netName: 'LD_PCN', label: 'Laser PCN', detail: 'Laser driver low/high current selector and PWM test pin.' },
  { gpioNum: 47, modulePin: 24, side: 'right', signalClass: 'control', netName: 'TEC_TEMPGD', label: 'TEC settle good', detail: 'Temperature settled input from the TEC controller.' },
  { gpioNum: 48, modulePin: 25, side: 'right', signalClass: 'control', netName: 'ERM_EN', label: 'ERM enable', detail: 'Dedicated enable pin for the DRV2605 haptic driver.' },
  { gpioNum: 45, modulePin: 26, side: 'right', signalClass: 'strap', netName: 'GPIO_45', label: 'Reserved test pin', detail: 'Reserved/open test point only.', riskNote: 'Reserved strap/test pin; do not use in production logic.' },
  { gpioNum: 0, modulePin: 27, side: 'right', signalClass: 'strap', netName: 'ESP_GPIO0', label: 'BOOT button', detail: 'Boot strap and download-mode pushbutton input.', riskNote: 'Changing GPIO0 can affect boot and flashing behavior.' },
  { gpioNum: 35, modulePin: 28, side: 'right', signalClass: 'bus', netName: 'GPIO35', label: 'Alt ERM SDA', detail: 'DNP alternate ERM I2C SDA route.' },
  { gpioNum: 36, modulePin: 29, side: 'right', signalClass: 'bus', netName: 'GPIO36', label: 'Alt ERM SCL', detail: 'DNP alternate ERM I2C SCL route.' },
  { gpioNum: 37, modulePin: 30, side: 'right', signalClass: 'control', netName: 'ERM_TRIG / GN_LD_EN', label: 'Shared ERM/green', detail: 'Hazardous shared net: DRV2605 trigger and green alignment laser enable.' },
  { gpioNum: 38, modulePin: 31, side: 'right', signalClass: 'bus', netName: 'IMU_SDI', label: 'IMU MOSI', detail: 'SPI MOSI to the LSM6DSO.' },
  { gpioNum: 39, modulePin: 32, side: 'right', signalClass: 'bus', netName: 'IMU_CS', label: 'IMU CS', detail: 'SPI chip-select to the LSM6DSO.' },
  { gpioNum: 40, modulePin: 33, side: 'right', signalClass: 'bus', netName: 'IMU_SCLK', label: 'IMU SCLK', detail: 'SPI clock to the LSM6DSO.' },
  { gpioNum: 41, modulePin: 34, side: 'right', signalClass: 'bus', netName: 'IMU_SDO', label: 'IMU MISO', detail: 'SPI MISO from the LSM6DSO.' },
  { gpioNum: 42, modulePin: 35, side: 'right', signalClass: 'control', netName: 'IMU_INT2', label: 'IMU INT2', detail: 'IMU interrupt/data-ready assist input.' },
  { gpioNum: 44, modulePin: 36, side: 'right', signalClass: 'debug', netName: 'ESP_RX', label: 'UART0 RX', detail: 'Onboard debug UART RX.', riskNote: 'Overriding debug UART pins can break wired debug sessions.' },
  { gpioNum: 43, modulePin: 37, side: 'right', signalClass: 'debug', netName: 'ESP_TX', label: 'UART0 TX', detail: 'Onboard debug UART TX.', riskNote: 'Overriding debug UART pins can break wired debug sessions.' },
  { gpioNum: 2, modulePin: 38, side: 'right', signalClass: 'analog', netName: 'LD_LIO', label: 'Laser current monitor', detail: 'Analog laser current telemetry input.', analogTelemetry: 'ldCurrentMonitor' },
  { gpioNum: 1, modulePin: 39, side: 'right', signalClass: 'analog', netName: 'LD_TMO', label: 'Laser temp monitor', detail: 'Analog laser driver temperature input.', analogTelemetry: 'ldDriverTemp' },
]

export function makeDefaultGpioInspectorStatus(): GpioInspectorStatus {
  return {
    anyOverrideActive: false,
    activeOverrideCount: 0,
    pins: gpioModulePins.map<GpioPinReadback>((pin) => ({
      gpioNum: pin.gpioNum,
      modulePin: pin.modulePin,
      outputCapable: true,
      inputEnabled: false,
      outputEnabled: false,
      openDrainEnabled: false,
      pullupEnabled: false,
      pulldownEnabled: false,
      levelHigh: false,
      overrideActive: false,
      overrideMode: 'firmware',
      overrideLevelHigh: false,
      overridePullupEnabled: false,
      overridePulldownEnabled: false,
    })),
  }
}
