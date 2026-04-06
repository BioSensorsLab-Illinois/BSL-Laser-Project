"""
Raspberry Pi platform implementation for GPIO configuration.

This module provides the concrete RpiPlatform class that implements the
Platform ABC for Raspberry Pi GPIO validation and code generation.

Supports variants: rpi3, rpi4, rpi5, rpi_zero2w

Python 3.9+ compatible. Uses only standard library modules.
"""

from typing import Any, Dict, List, Optional

from .base import (
    ConflictType,
    GenerationResult,
    PeripheralGroup,
    Pin,
    Platform,
    ValidationResult,
)


class RpiPlatform(Platform):
    """
    Concrete Platform implementation for Raspberry Pi GPIO.

    Provides complete GPIO pin database for all 28 BCM GPIO pins (GPIO0-GPIO27),
    peripheral group definitions for SPI, I2C, UART, PWM, and PCM, and validation
    logic for detecting conflicts, reserved pins, and electrical constraints.

    Supports Pi 3B/3B+, Pi 4B, Pi 5, and Pi Zero 2W variants. The main difference
    between variants is the UART/Bluetooth conflict (present on Pi 3/4/Zero2W,
    absent on Pi 5).

    Attributes:
        name: Always "rpi" for Raspberry Pi.
        variant: One of "rpi3", "rpi4", "rpi5", "rpi_zero2w".
        module: Always None for Raspberry Pi (no module types).

    Example:
        >>> platform = RpiPlatform("rpi", "rpi4")
        >>> pin = platform.get_pin(17)
        >>> print(pin.usability)
        free
        >>> result = platform.validate(assignment_dict)
        >>> print(result.valid)
        True
    """

    def __init__(
        self,
        name: str,
        variant: str,
        module: Optional[str] = None
    ) -> None:
        """
        Initialize the Raspberry Pi platform.

        Args:
            name: Platform identifier, should be "rpi".
            variant: Pi variant - "rpi3", "rpi4", "rpi5", or "rpi_zero2w".
            module: Ignored for Raspberry Pi (always set to None).
        """
        super().__init__(name, variant, None)
        self._pins = self._build_pins()
        self._groups = self._build_peripheral_groups()

    def _build_pins(self) -> Dict[int, Pin]:
        """Build the complete pin database for Raspberry Pi."""
        pins = {}

        # Common attributes for all RPi pins
        common = {
            "voltage": 3.3,
            "max_current_ma": 16.0,
            "can_input": True,
            "can_output": True,
            "adc_channel": None,
            "dac_channel": None,
            "touch_channel": None,
            "internal_pulls": ["up", "down"],
        }

        # GPIO0 - HAT EEPROM I2C0 SDA
        pins[0] = Pin(
            gpio_num=0,
            name="GPIO0",
            physical_pin=27,
            default_state="pull_up",
            alternate_functions={"ALT0": "SDA0"},
            pwm_capable=False,
            usability="reserved",
            usability_reason="HAT EEPROM I2C0 SDA",
            conflicts=["I2C0"],
            notes="Do not use — HAT EEPROM",
            **common
        )

        # GPIO1 - HAT EEPROM I2C0 SCL
        pins[1] = Pin(
            gpio_num=1,
            name="GPIO1",
            physical_pin=28,
            default_state="pull_up",
            alternate_functions={"ALT0": "SCL0"},
            pwm_capable=False,
            usability="reserved",
            usability_reason="HAT EEPROM I2C0 SCL",
            conflicts=["I2C0"],
            notes="Do not use — HAT EEPROM",
            **common
        )

        # GPIO2 - I2C1 SDA
        pins[2] = Pin(
            gpio_num=2,
            name="GPIO2",
            physical_pin=3,
            default_state="pull_up",
            alternate_functions={"ALT0": "SDA1"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO3 - I2C1 SCL
        pins[3] = Pin(
            gpio_num=3,
            name="GPIO3",
            physical_pin=5,
            default_state="pull_up",
            alternate_functions={"ALT0": "SCL1"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO4 - GPCLK0
        pins[4] = Pin(
            gpio_num=4,
            name="GPIO4",
            physical_pin=7,
            default_state="pull_up",
            alternate_functions={"ALT0": "GPCLK0"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO5 - GPCLK1
        pins[5] = Pin(
            gpio_num=5,
            name="GPIO5",
            physical_pin=29,
            default_state="pull_up",
            alternate_functions={"ALT0": "GPCLK1"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO6 - GPCLK2
        pins[6] = Pin(
            gpio_num=6,
            name="GPIO6",
            physical_pin=31,
            default_state="pull_up",
            alternate_functions={"ALT0": "GPCLK2"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO7 - SPI0_CE1
        pins[7] = Pin(
            gpio_num=7,
            name="GPIO7",
            physical_pin=26,
            default_state="pull_up",
            alternate_functions={"ALT0": "SPI0_CE1"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO8 - SPI0_CE0
        pins[8] = Pin(
            gpio_num=8,
            name="GPIO8",
            physical_pin=24,
            default_state="pull_up",
            alternate_functions={"ALT0": "SPI0_CE0"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO9 - SPI0_MISO
        pins[9] = Pin(
            gpio_num=9,
            name="GPIO9",
            physical_pin=21,
            default_state="pull_down",
            alternate_functions={"ALT0": "SPI0_MISO"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO10 - SPI0_MOSI
        pins[10] = Pin(
            gpio_num=10,
            name="GPIO10",
            physical_pin=19,
            default_state="pull_down",
            alternate_functions={"ALT0": "SPI0_MOSI"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO11 - SPI0_SCLK
        pins[11] = Pin(
            gpio_num=11,
            name="GPIO11",
            physical_pin=23,
            default_state="pull_down",
            alternate_functions={"ALT0": "SPI0_SCLK"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO12 - PWM0
        pins[12] = Pin(
            gpio_num=12,
            name="GPIO12",
            physical_pin=32,
            default_state="pull_down",
            alternate_functions={"ALT0": "PWM0"},
            pwm_capable=True,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO13 - PWM1
        pins[13] = Pin(
            gpio_num=13,
            name="GPIO13",
            physical_pin=33,
            default_state="pull_down",
            alternate_functions={"ALT0": "PWM1"},
            pwm_capable=True,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO14 - UART0 TX (BT conflict on Pi 3/4/Zero2W)
        pins[14] = Pin(
            gpio_num=14,
            name="GPIO14",
            physical_pin=8,
            default_state="pull_down",
            alternate_functions={"ALT0": "TXD0"},
            pwm_capable=False,
            usability="restricted",
            usability_reason="UART0 TX — BT conflict on Pi 3/4/Zero2W",
            conflicts=["UART0"],
            notes="Bluetooth conflict on Pi 3/4/Zero2W",
            **common
        )

        # GPIO15 - UART0 RX (BT conflict on Pi 3/4/Zero2W)
        pins[15] = Pin(
            gpio_num=15,
            name="GPIO15",
            physical_pin=10,
            default_state="pull_down",
            alternate_functions={"ALT0": "RXD0"},
            pwm_capable=False,
            usability="restricted",
            usability_reason="UART0 RX — BT conflict on Pi 3/4/Zero2W",
            conflicts=["UART0"],
            notes="Bluetooth conflict on Pi 3/4/Zero2W",
            **common
        )

        # GPIO16 - SPI1_CE2
        pins[16] = Pin(
            gpio_num=16,
            name="GPIO16",
            physical_pin=36,
            default_state="pull_down",
            alternate_functions={"ALT3": "CTS0", "ALT4": "SPI1_CE2"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO17 - SPI1_CE1
        pins[17] = Pin(
            gpio_num=17,
            name="GPIO17",
            physical_pin=11,
            default_state="pull_down",
            alternate_functions={"ALT3": "RTS0", "ALT4": "SPI1_CE1"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO18 - PCM_CLK / SPI1_CE0 / PWM0 (ALT5)
        pins[18] = Pin(
            gpio_num=18,
            name="GPIO18",
            physical_pin=12,
            default_state="pull_down",
            alternate_functions={"ALT0": "PCM_CLK", "ALT4": "SPI1_CE0", "ALT5": "PWM0"},
            pwm_capable=True,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO19 - PCM_FS / SPI1_MISO / PWM1 (ALT5)
        pins[19] = Pin(
            gpio_num=19,
            name="GPIO19",
            physical_pin=35,
            default_state="pull_down",
            alternate_functions={"ALT0": "PCM_FS", "ALT4": "SPI1_MISO", "ALT5": "PWM1"},
            pwm_capable=True,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO20 - PCM_DIN / SPI1_MOSI
        pins[20] = Pin(
            gpio_num=20,
            name="GPIO20",
            physical_pin=38,
            default_state="pull_down",
            alternate_functions={"ALT0": "PCM_DIN", "ALT4": "SPI1_MOSI"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO21 - PCM_DOUT / SPI1_SCLK
        pins[21] = Pin(
            gpio_num=21,
            name="GPIO21",
            physical_pin=40,
            default_state="pull_down",
            alternate_functions={"ALT0": "PCM_DOUT", "ALT4": "SPI1_SCLK"},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO22 - clean GPIO
        pins[22] = Pin(
            gpio_num=22,
            name="GPIO22",
            physical_pin=15,
            default_state="pull_down",
            alternate_functions={},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO23 - clean GPIO
        pins[23] = Pin(
            gpio_num=23,
            name="GPIO23",
            physical_pin=16,
            default_state="pull_down",
            alternate_functions={},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO24 - clean GPIO
        pins[24] = Pin(
            gpio_num=24,
            name="GPIO24",
            physical_pin=18,
            default_state="pull_down",
            alternate_functions={},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO25 - clean GPIO
        pins[25] = Pin(
            gpio_num=25,
            name="GPIO25",
            physical_pin=22,
            default_state="pull_down",
            alternate_functions={},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO26 - clean GPIO
        pins[26] = Pin(
            gpio_num=26,
            name="GPIO26",
            physical_pin=37,
            default_state="pull_down",
            alternate_functions={},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        # GPIO27 - clean GPIO
        pins[27] = Pin(
            gpio_num=27,
            name="GPIO27",
            physical_pin=13,
            default_state="pull_down",
            alternate_functions={},
            pwm_capable=False,
            usability="free",
            usability_reason=None,
            conflicts=[],
            notes="",
            **common
        )

        return pins

    def _build_peripheral_groups(self) -> Dict[str, PeripheralGroup]:
        """Build the peripheral group definitions."""
        groups = {}

        groups["SPI0"] = PeripheralGroup(
            name="SPI0",
            protocol="spi",
            pins={"MOSI": 10, "MISO": 9, "SCLK": 11, "CE0": 8, "CE1": 7},
            is_fixed=True,
            enabled_by="dtparam=spi=on",
            notes=""
        )

        groups["SPI1"] = PeripheralGroup(
            name="SPI1",
            protocol="spi",
            pins={"MOSI": 20, "MISO": 19, "SCLK": 21, "CE0": 18, "CE1": 17, "CE2": 16},
            is_fixed=True,
            enabled_by="dtoverlay=spi1-Ncs",
            notes=""
        )

        groups["I2C0"] = PeripheralGroup(
            name="I2C0",
            protocol="i2c",
            pins={"SDA": 0, "SCL": 1},
            is_fixed=True,
            enabled_by=None,
            notes="Reserved for HAT EEPROM — do not use"
        )

        groups["I2C1"] = PeripheralGroup(
            name="I2C1",
            protocol="i2c",
            pins={"SDA": 2, "SCL": 3},
            is_fixed=True,
            enabled_by="dtparam=i2c_arm=on",
            notes=""
        )

        groups["UART0"] = PeripheralGroup(
            name="UART0",
            protocol="uart",
            pins={"TX": 14, "RX": 15},
            is_fixed=True,
            enabled_by="enable_uart=1",
            notes=""
        )

        groups["PWM"] = PeripheralGroup(
            name="PWM",
            protocol="pwm",
            pins={"PWM0": 12, "PWM1": 13},
            is_fixed=False,
            enabled_by=None,
            notes="Also available on GPIO18(ALT5)/GPIO19(ALT5)"
        )

        groups["PCM"] = PeripheralGroup(
            name="PCM",
            protocol="pcm",
            pins={"CLK": 18, "FS": 19, "DIN": 20, "DOUT": 21},
            is_fixed=True,
            enabled_by=None,
            notes=""
        )

        return groups

    def get_pin(self, gpio_num: int) -> Pin:
        """
        Retrieve a specific pin by its BCM GPIO number.

        Args:
            gpio_num: BCM GPIO number (0-27).

        Returns:
            Pin object for the requested GPIO.

        Raises:
            ValueError: If gpio_num is not in range 0-27.
        """
        if gpio_num not in self._pins:
            raise ValueError(
                f"Invalid GPIO number {gpio_num} for Raspberry Pi. "
                f"Valid range: 0-27 (BCM numbering)."
            )
        return self._pins[gpio_num]

    def get_all_pins(self) -> List[Pin]:
        """
        Retrieve all 28 GPIO pins sorted by GPIO number.

        Returns:
            List of all Pin objects, ordered by gpio_num ascending.
        """
        return [self._pins[i] for i in sorted(self._pins.keys())]

    def get_peripheral_group(self, name: str) -> PeripheralGroup:
        """
        Retrieve a peripheral group by name.

        Args:
            name: Peripheral group name (case-insensitive).

        Returns:
            PeripheralGroup object.

        Raises:
            ValueError: If no group with the given name exists.
        """
        name_upper = name.upper()
        if name_upper not in self._groups:
            available = ", ".join(sorted(self._groups.keys()))
            raise ValueError(
                f"Unknown peripheral group '{name}'. "
                f"Available groups: {available}."
            )
        return self._groups[name_upper]

    def get_all_peripheral_groups(self) -> List[PeripheralGroup]:
        """
        Retrieve all peripheral groups.

        Returns:
            List of all PeripheralGroup objects.
        """
        return list(self._groups.values())

    def get_groups_for_protocol(self, protocol: str) -> List[PeripheralGroup]:
        """
        Retrieve all peripheral groups that use a specific protocol.

        Args:
            protocol: Protocol identifier (e.g., "spi", "i2c", "uart").

        Returns:
            List of PeripheralGroup objects matching the protocol.
        """
        protocol_lower = protocol.lower()
        return [g for g in self._groups.values() if g.protocol == protocol_lower]

    def validate(self, assignment: Dict[str, Any]) -> ValidationResult:
        """
        Validate a proposed GPIO pin assignment configuration.

        Runs validation checks in order:
        1. RESERVED_PIN - GPIO0/1 cannot be assigned
        2. DIRECT_CONFLICT - Same GPIO assigned multiple times
        3. PERIPHERAL_GROUP - Pin claimed by active peripheral group
        4. UART_BT_CONFLICT - UART0 vs Bluetooth on Pi 3/4/Zero2W
        5. PWM_AUDIO_CONFLICT - PWM on GPIO18/19 disables audio
        6. BOOT_STATE - Pull-up default pins may affect devices at boot
        7. MISSING_PULLUP - I2C/1-Wire needs external pull-ups
        8. ELECTRICAL_CURRENT - Total current draw check

        Args:
            assignment: Pin assignment dict with keys: platform, variant,
                module, wifi_enabled, pins (list of pin assignments).

        Returns:
            ValidationResult with valid flag, errors, warnings, and summary.
        """
        errors = []  # type: List[Dict[str, Any]]
        warnings = []  # type: List[Dict[str, Any]]
        pins_list = assignment.get("pins", [])
        variant = assignment.get("variant", self.variant)

        # Track seen GPIOs for conflict detection
        seen_gpios = {}  # type: Dict[int, str]

        # Track active peripheral groups
        active_groups = set()  # type: set

        # First pass: identify active peripheral groups
        for pin_assignment in pins_list:
            gpio = pin_assignment.get("gpio")
            function = pin_assignment.get("function", "")
            protocol_bus = pin_assignment.get("protocol_bus", "")

            # Check if this pin activates a peripheral group
            for group_name, group in self._groups.items():
                # Check if GPIO matches a pin in the group
                if gpio in group.pins.values():
                    # Check if function or protocol matches
                    if (protocol_bus.lower() == group.protocol or
                            function.upper() in [s.upper() for s in group.pins.keys()] or
                            any(sig.upper() in function.upper() for sig in group.pins.keys())):
                        active_groups.add(group_name)

        # Second pass: run all validation checks
        for pin_assignment in pins_list:
            gpio = pin_assignment.get("gpio")
            function = pin_assignment.get("function", "")
            protocol_bus = pin_assignment.get("protocol_bus", "")
            pull = pin_assignment.get("pull", "none")
            device = pin_assignment.get("device", "device")

            # Check 1: RESERVED_PIN
            if gpio in (0, 1):
                errors.append({
                    "gpio": gpio,
                    "code": ConflictType.RESERVED_PIN.value,
                    "message": f"GPIO{gpio} is reserved for HAT EEPROM (I2C0). "
                               f"Never assign user functions to this pin.",
                    "severity": "error"
                })

            # Check 2: DIRECT_CONFLICT
            if gpio in seen_gpios:
                errors.append({
                    "gpio": gpio,
                    "code": ConflictType.DIRECT_CONFLICT.value,
                    "message": f"GPIO{gpio} is assigned multiple times: "
                               f"'{seen_gpios[gpio]}' and '{function}'.",
                    "severity": "error"
                })
            else:
                seen_gpios[gpio] = function

            # Check 3: PERIPHERAL_GROUP conflict
            for group_name in active_groups:
                group = self._groups[group_name]
                if gpio in group.pins.values():
                    # Find expected signal for this GPIO
                    expected_signal = None
                    for sig, pin in group.pins.items():
                        if pin == gpio:
                            expected_signal = sig
                            break

                    # Check if function matches expected signal
                    if expected_signal:
                        func_upper = function.upper()
                        sig_upper = expected_signal.upper()
                        # Allow if function contains the signal name
                        if sig_upper not in func_upper and func_upper != sig_upper:
                            # Check if it's truly a conflict (not same protocol use)
                            if protocol_bus.lower() != group.protocol:
                                errors.append({
                                    "gpio": gpio,
                                    "code": ConflictType.PERIPHERAL_GROUP.value,
                                    "message": f"GPIO{gpio} is claimed by {group_name} "
                                               f"({expected_signal}). Cannot assign to "
                                               f"'{function}'.",
                                    "severity": "error"
                                })

            # Check 4: UART_BT_CONFLICT (warning)
            if variant in ("rpi3", "rpi4", "rpi_zero2w") and gpio in (14, 15):
                warnings.append({
                    "gpio": gpio,
                    "code": ConflictType.UART_BT_CONFLICT.value,
                    "message": f"GPIO{gpio} (UART0) conflicts with Bluetooth on "
                               f"Pi 3/4/Zero2W. Use `dtoverlay=disable-bt` or "
                               f"`dtoverlay=miniuart-bt`.",
                    "severity": "warning"
                })

            # Check 5: PWM_AUDIO_CONFLICT (warning)
            if gpio in (18, 19):
                if protocol_bus.lower() == "pwm" or "PWM" in function.upper():
                    warnings.append({
                        "gpio": gpio,
                        "code": ConflictType.ALT_FUNCTION.value,
                        "message": f"GPIO{gpio} as PWM will disable analog audio "
                                   f"(3.5mm jack). Use GPIO12/13 for PWM if audio "
                                   f"needed.",
                        "severity": "warning"
                    })

            # Check 6: BOOT_STATE (warning)
            # GPIO2-8 have pull-up default (GPIO0/1 already caught by RESERVED_PIN)
            if gpio in range(2, 9):
                pin = self._pins.get(gpio)
                if pin and pin.default_state == "pull_up":
                    # Only warn for output functions
                    if self._is_output_function(function, protocol_bus):
                        warnings.append({
                            "gpio": gpio,
                            "code": ConflictType.ALT_FUNCTION.value,
                            "message": f"GPIO{gpio} defaults to HIGH at boot "
                                       f"(internal pull-up). This may affect "
                                       f"{device} before initialization.",
                            "severity": "warning"
                        })

            # Check 7: MISSING_PULLUP (warning)
            if protocol_bus.lower() in ("i2c", "onewire"):
                if pull != "external_up":
                    warnings.append({
                        "gpio": gpio,
                        "code": ConflictType.MISSING_PULLUP.value,
                        "message": f"GPIO{gpio} ({protocol_bus}) requires external "
                                   f"pull-up (4.7kΩ recommended). Internal ~50kΩ "
                                   f"is too weak.",
                        "severity": "warning"
                    })

        # Check 8: Current estimation
        estimated_current = self._estimate_current(pins_list)

        if estimated_current > 50:
            errors.append({
                "gpio": -1,
                "code": ConflictType.ELECTRICAL_CURRENT.value,
                "message": f"Estimated total GPIO current {estimated_current}mA "
                           f"exceeds Raspberry Pi limit of 50mA.",
                "severity": "error"
            })
        elif estimated_current > 40:
            warnings.append({
                "gpio": -1,
                "code": ConflictType.ELECTRICAL_CURRENT.value,
                "message": f"Estimated GPIO current {estimated_current}mA is "
                           f"approaching the 50mA limit.",
                "severity": "warning"
            })

        return ValidationResult(
            valid=len(errors) == 0,
            errors=errors,
            warnings=warnings,
            summary={
                "total_pins": len(pins_list),
                "errors": len(errors),
                "warnings": len(warnings),
                "current_draw_ma": estimated_current
            }
        )

    def _is_output_function(self, function: str, protocol_bus: str) -> bool:
        """Check if a function implies output capability."""
        output_keywords = ["output", "out", "led", "relay", "motor", "pwm",
                          "mosi", "sclk", "tx", "clk", "dout"]
        func_lower = function.lower()
        proto_lower = protocol_bus.lower()

        for keyword in output_keywords:
            if keyword in func_lower:
                return True

        # Protocol-based detection
        if proto_lower in ("spi", "uart", "pwm", "pcm"):
            return True
        if proto_lower == "gpio" and "out" in func_lower:
            return True

        return False

    def _estimate_current(self, pins_list: List[Dict[str, Any]]) -> float:
        """
        Estimate total current draw from pin assignments.

        Uses 3mA per output pin as default estimate.
        """
        total = 0.0
        for pin_assignment in pins_list:
            function = pin_assignment.get("function", "")
            protocol_bus = pin_assignment.get("protocol_bus", "")

            if self._is_output_function(function, protocol_bus):
                total += 3.0  # mA estimated draw per output pin (RPi conservative default)

        return total

    # GPIO to physical pin mapping
    _GPIO_TO_PHYSICAL = {
        0: 27, 1: 28, 2: 3, 3: 5, 4: 7, 5: 29, 6: 31, 7: 26,
        8: 24, 9: 21, 10: 19, 11: 23, 12: 32, 13: 33, 14: 8,
        15: 10, 16: 36, 17: 11, 18: 12, 19: 35, 20: 38, 21: 40,
        22: 15, 23: 16, 24: 18, 25: 22, 26: 37, 27: 13
    }

    # SPI0 pins
    _SPI0_PINS = {7, 8, 9, 10, 11}

    # SPI1 pins
    _SPI1_PINS = {16, 17, 18, 19, 20, 21}

    def _get_physical_pin(self, gpio: int) -> int:
        """Get the physical pin number for a GPIO."""
        return self._GPIO_TO_PHYSICAL.get(gpio, 0)

    def _sanitize_var_name(self, name: str, gpio: int, function: str) -> str:
        """
        Convert a device name to a valid Python identifier.

        Args:
            name: Device name (may be empty).
            gpio: GPIO number (used if name is empty).
            function: Function name (used if name is empty).

        Returns:
            Valid Python identifier.
        """
        if not name or not name.strip():
            # Use function + gpio if no device name
            name = f"{function}_{gpio}"

        # Lowercase and replace non-alphanumeric with underscore
        result = ""
        for char in name.lower():
            if char.isalnum():
                result += char
            elif char in (" ", "-", "_"):
                result += "_"

        # Remove consecutive underscores
        while "__" in result:
            result = result.replace("__", "_")

        # Remove leading/trailing underscores
        result = result.strip("_")

        # Ensure it doesn't start with a digit
        if result and result[0].isdigit():
            result = "dev_" + result

        return result if result else f"gpio_{gpio}"

    def _get_config_path_comment(self, variant: str) -> str:
        """Get the config.txt path comment for a variant."""
        if variant in ("rpi3", "rpi_zero2w"):
            return "# Add to /boot/config.txt"
        elif variant == "rpi4":
            return "# Add to /boot/config.txt or /boot/firmware/config.txt"
        elif variant == "rpi5":
            return "# Add to /boot/firmware/config.txt"
        else:
            return "# Add to /boot/config.txt"

    def generate_config(self, assignment: Dict[str, Any]) -> GenerationResult:
        """
        Generate platform configuration for a Raspberry Pi pin assignment.

        Produces config.txt entries, wiring notes, and warnings for the
        given assignment.

        Args:
            assignment: Pin assignment dict with keys: platform, variant,
                module, wifi_enabled, pins (list of pin assignments).

        Returns:
            GenerationResult with config_lines, wiring_notes, warnings,
            and empty init_code (code is produced by generate_code()).
        """
        pins_list = assignment.get("pins", [])
        variant = assignment.get("variant", self.variant)

        config_lines = []  # type: List[str]
        wiring_notes = []  # type: List[str]
        warnings = []  # type: List[str]

        # Track which protocols are used
        has_i2c = False
        has_spi0 = False
        has_spi1 = False
        spi1_cs_count = 0
        has_uart = False
        pwm_pins = set()  # type: set
        onewire_pins = []  # type: List[int]

        # First pass: identify protocols and build wiring notes
        for pin in pins_list:
            gpio = pin.get("gpio", 0)
            function = pin.get("function", "")
            protocol = pin.get("protocol_bus", "").lower()
            device = pin.get("device", "")
            pull = pin.get("pull", "none")
            notes = pin.get("notes", "")
            physical = self._get_physical_pin(gpio)

            # Track protocols
            if protocol == "i2c":
                has_i2c = True
            elif protocol == "spi":
                if gpio in self._SPI0_PINS:
                    has_spi0 = True
                elif gpio in self._SPI1_PINS:
                    has_spi1 = True
                    if "ce" in function.lower() or "cs" in function.lower():
                        spi1_cs_count += 1
            elif protocol == "uart":
                has_uart = True
            elif protocol == "pwm" or "pwm" in function.lower():
                pwm_pins.add(gpio)
            elif protocol in ("1wire", "onewire") or "1-wire" in function.lower():
                onewire_pins.append(gpio)

            # Build wiring note
            wiring_note = self._build_wiring_note(
                gpio, physical, function, protocol, device, pull, notes
            )
            wiring_notes.append(wiring_note)

        # Add config path comment
        config_lines.append(self._get_config_path_comment(variant))
        config_lines.append("")

        # Add I2C config
        if has_i2c:
            config_lines.append("dtparam=i2c_arm=on")

        # Add SPI0 config
        if has_spi0:
            config_lines.append("dtparam=spi=on")

        # Add SPI1 config
        if has_spi1:
            cs_suffix = "1cs" if spi1_cs_count <= 1 else f"{min(spi1_cs_count, 3)}cs"
            config_lines.append(f"dtoverlay=spi1-{cs_suffix}")

        # Add UART config
        if has_uart:
            config_lines.append("enable_uart=1")
            if variant in ("rpi3", "rpi4", "rpi_zero2w"):
                config_lines.append(
                    "# Consider: dtoverlay=disable-bt or dtoverlay=miniuart-bt for full UART"
                )

        # Add PWM config
        if pwm_pins:
            pwm_config = self._build_pwm_config(pwm_pins)
            config_lines.extend(pwm_config)

        # Add 1-Wire config
        for gpio in onewire_pins:
            if gpio == 4:
                config_lines.append("dtoverlay=w1-gpio")
            else:
                config_lines.append(f"dtoverlay=w1-gpio,gpiopin={gpio}")

        # Add GPIO comments for plain GPIO pins
        for pin in pins_list:
            gpio = pin.get("gpio", 0)
            function = pin.get("function", "").upper()
            protocol = pin.get("protocol_bus", "").lower()
            device = pin.get("device", "")

            if protocol == "gpio" and function in ("OUTPUT", "INPUT"):
                config_lines.append(
                    f"# GPIO{gpio}: {device} {function.lower()}"
                )

        # Add protocol-level wiring notes
        if has_i2c:
            wiring_notes.append(
                "Add 4.7kΩ pull-up resistors on SDA and SCL to 3.3V"
            )

        for gpio in onewire_pins:
            wiring_notes.append(
                "Add 4.7kΩ pull-up resistor on 1-Wire data line to 3.3V"
            )
            break  # Only add once

        # Add level shifter note if any device might need 5V
        wiring_notes.append(
            "Use 3.3V logic levels. For 5V devices, add a level shifter."
        )

        # Add power note
        wiring_notes.append(
            "Power: 3.3V available on pin 1 or 17. 5V available on pin 2 or 4. "
            "Ground on pins 6, 9, 14, 20, 25, 30, 34, 39."
        )

        # Generate warnings
        if has_uart and variant in ("rpi3", "rpi4", "rpi_zero2w"):
            warnings.append(
                "UART0 on Pi 3/4/Zero2W shares pins with Bluetooth. "
                "Add dtoverlay=disable-bt to config.txt for full UART, "
                "or use dtoverlay=miniuart-bt to move BT to mini-UART."
            )

        return GenerationResult(
            config_lines=config_lines,
            init_code="",
            wiring_notes=wiring_notes,
            warnings=warnings,
            alternatives=[]
        )

    def _build_wiring_note(
        self,
        gpio: int,
        physical: int,
        function: str,
        protocol: str,
        device: str,
        pull: str,
        notes: str
    ) -> str:
        """Build a wiring note for a single pin."""
        func_upper = function.upper()
        device_str = device if device else "device"

        # Determine context based on protocol and function
        if protocol == "i2c":
            if "sda" in function.lower():
                context = "SDA"
            elif "scl" in function.lower():
                context = "SCL"
            else:
                context = function
        elif protocol == "spi":
            context = function  # MOSI, MISO, SCLK, CE0, etc.
        elif protocol == "uart":
            context = f"{function} (cross TX↔RX with device)"
        elif func_upper == "OUTPUT":
            context = "anode via 330Ω resistor" if "led" in device.lower() else "output"
        elif func_upper == "INPUT":
            if pull in ("internal_up", "up"):
                context = "input with internal pull-up"
            elif pull in ("internal_down", "down"):
                context = "input with internal pull-down"
            else:
                context = "input"
        else:
            context = function

        note = f"Connect {device_str} {context} to GPIO{gpio} (physical pin {physical})"

        if notes:
            note += f". {notes}"

        return note

    def _build_pwm_config(self, pwm_pins: set) -> List[str]:
        """Build PWM overlay config lines."""
        config = []

        # Check for GPIO12/13 pair
        has_12 = 12 in pwm_pins
        has_13 = 13 in pwm_pins

        # Check for GPIO18/19 pair
        has_18 = 18 in pwm_pins
        has_19 = 19 in pwm_pins

        if has_12 and has_13:
            config.append("dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4")
        elif has_12:
            config.append("dtoverlay=pwm,pin=12,func=4")
        elif has_13:
            # GPIO13 needs GPIO12 for PWM1 channel
            config.append("dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4")

        if has_18 and has_19:
            config.append("dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2")
        elif has_18:
            config.append("dtoverlay=pwm,pin=18,func=2")
        elif has_19:
            # GPIO19 needs GPIO18 for PWM1 channel
            config.append("dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2")

        return config

    def generate_code(self, assignment: Dict[str, Any], framework: str) -> str:
        """
        Generate initialization code for a Raspberry Pi pin assignment.

        Produces complete, runnable Python code for the specified framework.

        Args:
            assignment: Pin assignment dict (same format as validate() input).
            framework: Target framework - "gpiozero" or "rpigpio".

        Returns:
            Complete Python script as a string.

        Raises:
            ValueError: If the framework is not supported.
        """
        framework = framework.lower()
        if framework not in ("gpiozero", "rpigpio"):
            raise ValueError(
                f"Unsupported RPi framework: {framework}. "
                f"Use 'gpiozero' or 'rpigpio'."
            )

        if framework == "gpiozero":
            return self._generate_gpiozero_code(assignment)
        else:
            return self._generate_rpigpio_code(assignment)

    def _categorize_pins(
        self, pins_list: List[Dict[str, Any]]
    ) -> Dict[str, List[Dict[str, Any]]]:
        """Categorize pins by protocol/function."""
        categories = {
            "i2c": [],
            "spi": [],
            "uart": [],
            "pwm": [],
            "onewire": [],
            "gpio_output": [],
            "gpio_input": [],
        }

        for pin in pins_list:
            protocol = pin.get("protocol_bus", "").lower()
            function = pin.get("function", "").upper()

            if protocol == "i2c":
                categories["i2c"].append(pin)
            elif protocol == "spi":
                categories["spi"].append(pin)
            elif protocol == "uart":
                categories["uart"].append(pin)
            elif protocol == "pwm" or "PWM" in function:
                categories["pwm"].append(pin)
            elif protocol in ("1wire", "onewire") or "1-WIRE" in function:
                categories["onewire"].append(pin)
            elif function == "OUTPUT":
                categories["gpio_output"].append(pin)
            elif function == "INPUT":
                categories["gpio_input"].append(pin)
            elif protocol == "gpio":
                # Default GPIO to input
                categories["gpio_input"].append(pin)

        return categories

    def _build_header_docstring(
        self, assignment: Dict[str, Any], framework: str
    ) -> str:
        """Build the header docstring for generated code."""
        pins_list = assignment.get("pins", [])
        variant = assignment.get("variant", self.variant)

        lines = ['"""']
        lines.append(f"GPIO initialization script for Raspberry Pi ({variant})")
        lines.append(f"Framework: {framework}")
        lines.append("")
        lines.append("Pin assignments:")

        for pin in pins_list:
            gpio = pin.get("gpio", 0)
            physical = self._get_physical_pin(gpio)
            protocol = pin.get("protocol_bus", "")
            function = pin.get("function", "")
            device = pin.get("device", "")
            lines.append(
                f"  GPIO{gpio} (pin {physical}) - {protocol} {function} ({device})"
            )

        lines.append("")
        lines.append("Config requirements:")

        # Generate config lines for documentation
        config_result = self.generate_config(assignment)
        for line in config_result.config_lines:
            if line and not line.startswith("#"):
                lines.append(f"  {line}")

        lines.append('"""')
        return "\n".join(lines)

    def _generate_gpiozero_code(self, assignment: Dict[str, Any]) -> str:
        """Generate gpiozero framework code."""
        pins_list = assignment.get("pins", [])
        categories = self._categorize_pins(pins_list)

        lines = []

        # Shebang
        lines.append("#!/usr/bin/env python3")

        # Docstring
        lines.append(self._build_header_docstring(assignment, "gpiozero"))
        lines.append("")

        # Imports
        imports = self._build_gpiozero_imports(categories)
        lines.extend(imports)
        lines.append("")

        # Protocol initialization
        protocol_init = self._build_protocol_init(categories)
        if protocol_init:
            lines.extend(protocol_init)
            lines.append("")

        # GPIO device initialization
        device_init = self._build_gpiozero_devices(categories)
        if device_init:
            lines.extend(device_init)
            lines.append("")

        # Main function
        lines.append("")
        lines.append("def main():")
        lines.append('    """Main function with example usage."""')

        # Add example usage based on categories
        main_body = self._build_gpiozero_main(categories)
        lines.extend(main_body)

        lines.append("")
        lines.append("")
        lines.append('if __name__ == "__main__":')
        lines.append("    main()")
        lines.append("")

        return "\n".join(lines)

    def _build_gpiozero_imports(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build gpiozero import statements."""
        lines = []

        # Determine which gpiozero classes are needed
        gpiozero_classes = []

        if categories["gpio_output"]:
            led_count = sum(
                1 for p in categories["gpio_output"]
                if "led" in p.get("device", "").lower()
            )
            non_led_count = len(categories["gpio_output"]) - led_count
            if led_count > 0:
                gpiozero_classes.append("LED")
            if non_led_count > 0:
                gpiozero_classes.append("DigitalOutputDevice")

        if categories["gpio_input"]:
            gpiozero_classes.append("Button")

        if categories["pwm"]:
            servo_count = sum(
                1 for p in categories["pwm"]
                if "servo" in p.get("device", "").lower()
            )
            non_servo_count = len(categories["pwm"]) - servo_count
            if servo_count > 0:
                gpiozero_classes.append("Servo")
            if non_servo_count > 0:
                gpiozero_classes.append("PWMOutputDevice")

        if gpiozero_classes:
            lines.append(f"from gpiozero import {', '.join(sorted(set(gpiozero_classes)))}")

        # Protocol imports
        if categories["i2c"]:
            lines.append("import smbus2")

        if categories["spi"]:
            lines.append("import spidev")

        if categories["uart"]:
            lines.append("import serial")

        if categories["onewire"]:
            lines.append("# 1-Wire: read via sysfs at /sys/bus/w1/devices/")

        if not lines:
            lines.append("# No imports needed")

        return lines

    def _build_protocol_init(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build protocol initialization code."""
        lines = []

        if categories["i2c"]:
            lines.append("# I2C bus initialization")
            lines.append("i2c_bus = smbus2.SMBus(1)")

            # Add device address constants
            devices_seen = set()
            for pin in categories["i2c"]:
                device = pin.get("device", "").upper()
                if device and device not in devices_seen:
                    devices_seen.add(device)
                    var_name = self._sanitize_var_name(
                        device, pin.get("gpio", 0), "i2c"
                    ).upper()
                    lines.append(f"# {var_name}_ADDR = 0x??  # Set I2C address for {device}")
            lines.append("")

        if categories["spi"]:
            lines.append("# SPI initialization")
            lines.append("spi = spidev.SpiDev()")
            lines.append("spi.open(0, 0)  # Bus 0, Device 0")
            lines.append("spi.max_speed_hz = 1000000  # 1 MHz default SPI clock")
            lines.append("")

        if categories["uart"]:
            lines.append("# UART initialization")
            lines.append("uart = serial.Serial('/dev/serial0', 9600, timeout=1)")
            lines.append("")

        return lines

    def _build_gpiozero_devices(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build gpiozero device initialization."""
        lines = []

        # GPIO outputs
        if categories["gpio_output"]:
            lines.append("# GPIO outputs")
            for pin in categories["gpio_output"]:
                gpio = pin.get("gpio", 0)
                device = pin.get("device", "")
                notes = pin.get("notes", "")
                var_name = self._sanitize_var_name(device, gpio, "output")

                is_led = "led" in device.lower()
                if is_led:
                    lines.append(f"{var_name} = LED({gpio})  # {device}")
                else:
                    lines.append(f"{var_name} = DigitalOutputDevice({gpio})  # {device}")

                if notes:
                    lines[-1] += f" - {notes}"
            lines.append("")

        # GPIO inputs
        if categories["gpio_input"]:
            lines.append("# GPIO inputs")
            for pin in categories["gpio_input"]:
                gpio = pin.get("gpio", 0)
                device = pin.get("device", "")
                pull = pin.get("pull", "none")
                notes = pin.get("notes", "")
                var_name = self._sanitize_var_name(device, gpio, "input")

                if pull in ("internal_up", "up", "external_up"):
                    lines.append(f"{var_name} = Button({gpio}, pull_up=True)  # {device}")
                elif pull in ("internal_down", "down", "external_down"):
                    lines.append(f"{var_name} = Button({gpio}, pull_up=False)  # {device}")
                else:
                    lines.append(f"{var_name} = Button({gpio}, pull_up=None)  # {device}")

                if notes:
                    lines[-1] += f" - {notes}"
            lines.append("")

        # PWM
        if categories["pwm"]:
            lines.append("# PWM outputs")
            for pin in categories["pwm"]:
                gpio = pin.get("gpio", 0)
                device = pin.get("device", "")
                notes = pin.get("notes", "")
                var_name = self._sanitize_var_name(device, gpio, "pwm")

                is_servo = "servo" in device.lower()
                if is_servo:
                    lines.append(f"{var_name} = Servo({gpio})  # {device}")
                else:
                    lines.append(f"{var_name} = PWMOutputDevice({gpio}, frequency=1000)  # {device}")

                if notes:
                    lines[-1] += f" - {notes}"
            lines.append("")

        return lines

    def _build_gpiozero_main(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build gpiozero main function body."""
        lines = []

        if categories["gpio_output"]:
            pin = categories["gpio_output"][0]
            var_name = self._sanitize_var_name(
                pin.get("device", ""), pin.get("gpio", 0), "output"
            )
            lines.append(f"    # Example: toggle {var_name}")
            lines.append(f"    # {var_name}.on()")
            lines.append(f"    # {var_name}.off()")

        if categories["gpio_input"]:
            pin = categories["gpio_input"][0]
            var_name = self._sanitize_var_name(
                pin.get("device", ""), pin.get("gpio", 0), "input"
            )
            lines.append(f"    # Example: check {var_name}")
            lines.append(f"    # if {var_name}.is_pressed:")
            lines.append(f'    #     print("{var_name} pressed")')

        if categories["pwm"]:
            pin = categories["pwm"][0]
            var_name = self._sanitize_var_name(
                pin.get("device", ""), pin.get("gpio", 0), "pwm"
            )
            lines.append(f"    # Example: set PWM {var_name}")
            lines.append(f"    # {var_name}.value = 0.5  # 50% duty cycle")

        if categories["i2c"]:
            lines.append("    # Example: read I2C device")
            lines.append("    # data = i2c_bus.read_byte_data(DEVICE_ADDR, 0x00)")

        if categories["spi"]:
            lines.append("    # Example: SPI transfer")
            lines.append("    # response = spi.xfer2([0x00, 0x00])")

        if categories["uart"]:
            lines.append("    # Example: UART read/write")
            lines.append("    # uart.write(b'Hello')")
            lines.append("    # data = uart.readline()")

        if not lines:
            lines.append("    pass")

        return lines

    def _generate_rpigpio_code(self, assignment: Dict[str, Any]) -> str:
        """Generate RPi.GPIO framework code."""
        pins_list = assignment.get("pins", [])
        categories = self._categorize_pins(pins_list)

        lines = []

        # Shebang
        lines.append("#!/usr/bin/env python3")

        # Docstring
        lines.append(self._build_header_docstring(assignment, "RPi.GPIO"))
        lines.append("")

        # Imports
        imports = self._build_rpigpio_imports(categories)
        lines.extend(imports)
        lines.append("")

        # GPIO setup
        lines.append("# GPIO setup")
        lines.append("GPIO.setmode(GPIO.BCM)")
        lines.append("GPIO.setwarnings(False)")
        lines.append("")

        # Protocol initialization
        protocol_init = self._build_protocol_init(categories)
        if protocol_init:
            lines.extend(protocol_init)

        # Pin constants
        pin_constants = self._build_rpigpio_constants(categories)
        if pin_constants:
            lines.extend(pin_constants)
            lines.append("")

        # GPIO setup calls
        setup_calls = self._build_rpigpio_setup(categories)
        if setup_calls:
            lines.extend(setup_calls)
            lines.append("")

        # PWM objects
        pwm_init = self._build_rpigpio_pwm(categories)
        if pwm_init:
            lines.extend(pwm_init)
            lines.append("")

        # Main function
        lines.append("")
        lines.append("def main():")
        lines.append('    """Main function with example usage."""')
        lines.append("    try:")

        # Add example usage
        main_body = self._build_rpigpio_main(categories)
        lines.extend(main_body)

        lines.append("    finally:")
        lines.append("        # Cleanup GPIO on exit")
        lines.append("        GPIO.cleanup()")

        lines.append("")
        lines.append("")
        lines.append('if __name__ == "__main__":')
        lines.append("    main()")
        lines.append("")

        return "\n".join(lines)

    def _build_rpigpio_imports(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build RPi.GPIO import statements."""
        lines = []

        lines.append("import RPi.GPIO as GPIO")

        if categories["i2c"]:
            lines.append("import smbus2")

        if categories["spi"]:
            lines.append("import spidev")

        if categories["uart"]:
            lines.append("import serial")

        if categories["onewire"]:
            lines.append("# 1-Wire: read via sysfs at /sys/bus/w1/devices/")

        return lines

    def _build_rpigpio_constants(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build pin constant definitions."""
        lines = []
        lines.append("# Pin constants")

        all_pins = (
            categories["gpio_output"] +
            categories["gpio_input"] +
            categories["pwm"]
        )

        for pin in all_pins:
            gpio = pin.get("gpio", 0)
            device = pin.get("device", "")
            notes = pin.get("notes", "")
            var_name = self._sanitize_var_name(device, gpio, "pin").upper()

            line = f"{var_name}_PIN = {gpio}"
            if notes:
                line += f"  # {notes}"
            elif device:
                line += f"  # {device}"
            lines.append(line)

        return lines if len(lines) > 1 else []

    def _build_rpigpio_setup(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build GPIO setup calls."""
        lines = []
        lines.append("# GPIO pin setup")

        # Outputs
        for pin in categories["gpio_output"]:
            gpio = pin.get("gpio", 0)
            device = pin.get("device", "")
            var_name = self._sanitize_var_name(device, gpio, "pin").upper()
            lines.append(f"GPIO.setup({var_name}_PIN, GPIO.OUT, initial=GPIO.LOW)")

        # Inputs
        for pin in categories["gpio_input"]:
            gpio = pin.get("gpio", 0)
            device = pin.get("device", "")
            pull = pin.get("pull", "none")
            var_name = self._sanitize_var_name(device, gpio, "pin").upper()

            if pull in ("internal_up", "up", "external_up"):
                lines.append(
                    f"GPIO.setup({var_name}_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)"
                )
            elif pull in ("internal_down", "down", "external_down"):
                lines.append(
                    f"GPIO.setup({var_name}_PIN, GPIO.IN, pull_up_down=GPIO.PUD_DOWN)"
                )
            else:
                lines.append(f"GPIO.setup({var_name}_PIN, GPIO.IN)")

        # PWM pins (setup as output)
        for pin in categories["pwm"]:
            gpio = pin.get("gpio", 0)
            device = pin.get("device", "")
            var_name = self._sanitize_var_name(device, gpio, "pin").upper()
            lines.append(f"GPIO.setup({var_name}_PIN, GPIO.OUT)")

        return lines if len(lines) > 1 else []

    def _build_rpigpio_pwm(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build PWM initialization."""
        lines = []

        if categories["pwm"]:
            lines.append("# PWM setup")
            for pin in categories["pwm"]:
                gpio = pin.get("gpio", 0)
                device = pin.get("device", "")
                var_name = self._sanitize_var_name(device, gpio, "pwm")
                const_name = self._sanitize_var_name(device, gpio, "pin").upper()

                lines.append(f"{var_name}_pwm = GPIO.PWM({const_name}_PIN, 1000)  # 1kHz")
                lines.append(f"{var_name}_pwm.start(0)  # Start with 0% duty cycle")

        return lines

    def _build_rpigpio_main(
        self, categories: Dict[str, List[Dict[str, Any]]]
    ) -> List[str]:
        """Build RPi.GPIO main function body."""
        lines = []

        if categories["gpio_output"]:
            pin = categories["gpio_output"][0]
            const_name = self._sanitize_var_name(
                pin.get("device", ""), pin.get("gpio", 0), "pin"
            ).upper()
            lines.append(f"        # Example: toggle output")
            lines.append(f"        # GPIO.output({const_name}_PIN, GPIO.HIGH)")
            lines.append(f"        # GPIO.output({const_name}_PIN, GPIO.LOW)")

        if categories["gpio_input"]:
            pin = categories["gpio_input"][0]
            const_name = self._sanitize_var_name(
                pin.get("device", ""), pin.get("gpio", 0), "pin"
            ).upper()
            lines.append(f"        # Example: read input")
            lines.append(f"        # if GPIO.input({const_name}_PIN):")
            lines.append(f'        #     print("Input is HIGH")')

        if categories["pwm"]:
            pin = categories["pwm"][0]
            var_name = self._sanitize_var_name(
                pin.get("device", ""), pin.get("gpio", 0), "pwm"
            )
            lines.append(f"        # Example: set PWM duty cycle")
            lines.append(f"        # {var_name}_pwm.ChangeDutyCycle(50)  # 50%")

        if categories["i2c"]:
            lines.append("        # Example: read I2C")
            lines.append("        # data = i2c_bus.read_byte_data(DEVICE_ADDR, 0x00)")

        if categories["spi"]:
            lines.append("        # Example: SPI transfer")
            lines.append("        # response = spi.xfer2([0x00, 0x00])")

        if categories["uart"]:
            lines.append("        # Example: UART")
            lines.append("        # uart.write(b'Hello')")

        # Always include pass since examples are commented out
        lines.append("        pass")

        return lines
