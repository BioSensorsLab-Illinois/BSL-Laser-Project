"""
Platform abstraction layer for GPIO configuration.

This package provides the abstract base class, data models, and factory
function for the gpio-config skill's platform-specific implementations.

Supported Platforms:
    - Raspberry Pi (rpi): Models 3B/3B+, 4B, 5, Zero 2W
    - ESP32: Variants ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6

Supported Modules (ESP32 only):
    - WROOM: Standard flash-only modules (GPIO6-11 reserved)
    - WROVER: Modules with PSRAM (GPIO6-11 and GPIO16-17 reserved)

Usage:
    >>> from platforms import get_platform, Pin, ValidationResult
    >>> platform = get_platform("esp32", variant="esp32s3", module="WROOM")
    >>> pin = platform.get_pin(21)
    >>> result = platform.validate(assignment_dict)

Note:
    Concrete platform implementations are registered in _PLATFORM_REGISTRY
    and instantiated by get_platform().
"""

from typing import Optional

from .base import (
    ConflictType,
    GenerationResult,
    PeripheralGroup,
    Pin,
    Platform,
    ValidationResult,
)
from .rpi import RpiPlatform
from .esp32 import Esp32Platform


__all__ = [
    "Pin",
    "PeripheralGroup",
    "ConflictType",
    "ValidationResult",
    "GenerationResult",
    "Platform",
    "get_platform",
]


# Registry of platform implementations.
# Maps (platform, variant) tuples to concrete Platform subclasses.
_PLATFORM_REGISTRY: dict = {
    # Raspberry Pi variants
    ("rpi", "rpi3"): RpiPlatform,
    ("rpi", "rpi4"): RpiPlatform,
    ("rpi", "rpi5"): RpiPlatform,
    ("rpi", "rpi_zero2w"): RpiPlatform,
    # ESP32 variants
    ("esp32", "esp32"): Esp32Platform,
    ("esp32", "esp32s2"): Esp32Platform,
    ("esp32", "esp32s3"): Esp32Platform,
    ("esp32", "esp32c3"): Esp32Platform,
    ("esp32", "esp32c6"): Esp32Platform,
}


def get_platform(
    platform: str,
    variant: Optional[str] = None,
    module: Optional[str] = None
) -> Platform:
    """
    Factory function to instantiate a platform-specific implementation.

    Creates and returns a concrete Platform subclass instance for the
    specified platform, variant, and module combination. This is the
    primary entry point for obtaining a Platform object.

    Supported Platform Identifiers:
        - "rpi" or "raspberry_pi": Raspberry Pi family
        - "esp32": ESP32 family

    Supported Variants:
        Raspberry Pi:
            - "rpi3": Raspberry Pi 3B and 3B+
            - "rpi4": Raspberry Pi 4B
            - "rpi5": Raspberry Pi 5
            - "rpi_zero2w": Raspberry Pi Zero 2W

        ESP32:
            - "esp32": Original ESP32 (dual-core Xtensa)
            - "esp32s2": ESP32-S2 (single-core Xtensa, no BT)
            - "esp32s3": ESP32-S3 (dual-core Xtensa, BLE5)
            - "esp32c3": ESP32-C3 (single-core RISC-V, BLE5)
            - "esp32c6": ESP32-C6 (RISC-V, WiFi 6, 802.15.4)

    Supported Module Types (ESP32 only):
        - "WROOM": Standard modules with flash only. Reserves GPIO6-11
            for the internal flash SPI bus.
        - "WROVER": Modules with PSRAM. Reserves GPIO6-11 for flash SPI
            and GPIO16-17 for PSRAM.
        - None: Use default reservations for the variant.

    Args:
        platform: Platform family identifier. Case-insensitive.
            Accepts "rpi", "raspberry_pi", "raspberrypi" for Raspberry Pi,
            or "esp32" for ESP32 family.
        variant: Specific model/variant identifier. If None, defaults to
            the most common variant for the platform (rpi4 for RPi,
            esp32 for ESP32). Case-insensitive.
        module: Module type for ESP32 platforms. Ignored for Raspberry Pi.
            If None for ESP32, defaults to "WROOM". Case-insensitive.

    Returns:
        A concrete Platform subclass instance configured for the specified
        platform, variant, and module.

    Raises:
        ValueError: If the platform identifier is not recognized, or if
            the variant is not valid for the platform. The error message
            lists all supported platforms and variants.

    Example:
        >>> # Get a Raspberry Pi 4 platform
        >>> rpi = get_platform("rpi", variant="rpi4")
        >>> print(rpi.name, rpi.variant)
        rpi rpi4

        >>> # Get an ESP32-S3 with WROOM module
        >>> esp = get_platform("esp32", variant="esp32s3", module="WROOM")
        >>> print(esp.name, esp.variant, esp.module)
        esp32 esp32s3 WROOM

        >>> # Invalid platform raises ValueError
        >>> get_platform("stm32")
        ValueError: Unsupported platform 'stm32'. Supported platforms: rpi, esp32
    """
    # Normalize platform string
    platform_lower = platform.lower().strip()

    # Normalize common aliases
    if platform_lower in ("raspberry_pi", "raspberrypi", "rpi"):
        platform_lower = "rpi"

    # Normalize variant names used as platform identifiers
    if platform_lower in ("rpi3", "rpi4", "rpi5", "rpi_zero2w"):
        platform_lower = "rpi"
    elif platform_lower in ("esp32s2", "esp32s3", "esp32c3", "esp32c6"):
        platform_lower = "esp32"

    # Define supported platforms and their default variants
    supported_platforms = {
        "rpi": {
            "variants": ["rpi3", "rpi4", "rpi5", "rpi_zero2w"],
            "default_variant": "rpi4",
            "modules": None,  # RPi doesn't use module types
        },
        "esp32": {
            "variants": ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"],
            "default_variant": "esp32",
            "modules": ["WROOM", "WROVER"],
            "default_module": "WROOM",
        },
    }

    # Check if platform is supported
    if platform_lower not in supported_platforms:
        supported_list = ", ".join(sorted(supported_platforms.keys()))
        raise ValueError(
            f"Unsupported platform '{platform}'. "
            f"Supported platforms: {supported_list}. "
            f"Supported variants - RPi: rpi3, rpi4, rpi5, rpi_zero2w; "
            f"ESP32: esp32, esp32s2, esp32s3, esp32c3, esp32c6. "
            f"Supported modules (ESP32 only): WROOM, WROVER."
        )

    platform_info = supported_platforms[platform_lower]

    # Normalize and validate variant
    if variant is None:
        variant = platform_info["default_variant"]
    else:
        variant = variant.lower().strip()

    if variant not in platform_info["variants"]:
        variant_list = ", ".join(platform_info["variants"])
        raise ValueError(
            f"Unsupported variant '{variant}' for platform '{platform_lower}'. "
            f"Supported variants: {variant_list}."
        )

    # Normalize and validate module (ESP32 only)
    if platform_lower == "esp32":
        if module is None:
            module = platform_info["default_module"]
        else:
            module = module.upper().strip()
            # Normalize common module name variations
            if module in ("WROOM32", "WROOM-32", "ESP32-WROOM", "ESP-WROOM"):
                module = "WROOM"
            elif module in ("WROVER32", "WROVER-32", "ESP32-WROVER", "ESP-WROVER"):
                module = "WROVER"
            if module not in platform_info["modules"]:
                module_list = ", ".join(platform_info["modules"])
                raise ValueError(
                    f"Unsupported module '{module}' for ESP32. "
                    f"Supported modules: {module_list}."
                )
    else:
        # Ignore module for non-ESP32 platforms
        module = None

    # Look up the concrete implementation in the registry
    registry_key = (platform_lower, variant)

    if registry_key in _PLATFORM_REGISTRY:
        platform_class = _PLATFORM_REGISTRY[registry_key]
        return platform_class(
            name=platform_lower,
            variant=variant,
            module=module
        )

    # No implementation registered yet (Phase 1 - implementations come later)
    raise ValueError(
        f"Platform '{platform_lower}' variant '{variant}' is recognized but "
        f"no implementation is available yet. Platform implementations will "
        f"be added in Phase 5 (Raspberry Pi) and Phase 6 (ESP32). "
        f"Supported platforms: rpi, esp32. "
        f"Supported variants - RPi: rpi3, rpi4, rpi5, rpi_zero2w; "
        f"ESP32: esp32, esp32s2, esp32s3, esp32c3, esp32c6. "
        f"Supported modules (ESP32 only): WROOM, WROVER."
    )
