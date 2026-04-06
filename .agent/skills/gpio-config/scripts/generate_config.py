#!/usr/bin/env python3
"""GPIO configuration and code generation script.

Reads a JSON pin assignment, validates it, generates platform configuration
and initialization code, and outputs the combined result.

Usage:
    echo '{"platform":"rpi4",...}' | python generate_config.py
    python generate_config.py input.json
    python generate_config.py --format text input.json
    python generate_config.py --framework rpigpio input.json
    python generate_config.py --validate-only input.json
    python generate_config.py --help
"""

import argparse
import json
import sys
from typing import Any, Dict, Optional

from platforms import get_platform


SUPPORTED_PLATFORMS = [
    "rpi3", "rpi4", "rpi5", "rpi_zero2w",
    "esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"
]

RPI_FRAMEWORKS = ("gpiozero", "rpigpio")
ESP32_FRAMEWORKS = ("arduino", "espidf")


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Generate GPIO configuration and initialization code."
    )
    parser.add_argument(
        "input_file",
        nargs="?",
        default=None,
        help="JSON input file (reads stdin if omitted)"
    )
    parser.add_argument(
        "--format",
        choices=["json", "text"],
        default="json",
        help="Output format (default: json)"
    )
    parser.add_argument(
        "--framework",
        type=str,
        default=None,
        help="Target framework. RPi: gpiozero (default), rpigpio. "
             "ESP32: arduino (default), espidf."
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Include additional detail in output"
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Run validation only, skip code generation"
    )
    return parser.parse_args()


def read_input(input_file: Optional[str]) -> Dict[str, Any]:
    """
    Read JSON input from file or stdin.

    Args:
        input_file: Path to input file, or None to read from stdin.

    Returns:
        Parsed JSON as a dictionary.

    Raises:
        SystemExit: On file not found or JSON parse error (exit code 2).
    """
    try:
        if input_file is not None and input_file != "-":
            with open(input_file, "r") as f:
                content = f.read()
        else:
            content = sys.stdin.read()

        return json.loads(content)

    except FileNotFoundError:
        print(f"Error: File not found: {input_file}", file=sys.stderr)
        sys.exit(2)
    except json.JSONDecodeError as e:
        print(f"Error: Failed to parse JSON input: {e}", file=sys.stderr)
        sys.exit(2)
    except IOError as e:
        print(f"Error: Failed to read input: {e}", file=sys.stderr)
        sys.exit(2)


def validate_input_structure(data: Dict[str, Any]) -> None:
    """
    Validate that the input JSON has required fields.

    Args:
        data: Parsed JSON input.

    Raises:
        SystemExit: On missing required fields (exit code 2).
    """
    if "platform" not in data:
        print("Error: Missing required field: 'platform'", file=sys.stderr)
        sys.exit(2)

    if "pins" not in data:
        print("Error: Missing required field: 'pins'", file=sys.stderr)
        sys.exit(2)

    if not isinstance(data["pins"], list):
        print("Error: 'pins' must be a list", file=sys.stderr)
        sys.exit(2)

    # Check each pin has required fields
    required_pin_fields = ["gpio", "function", "protocol_bus"]
    for i, pin in enumerate(data["pins"]):
        missing = [f for f in required_pin_fields if f not in pin]
        if missing:
            missing_str = "', '".join(missing)
            print(
                f"Error: Missing required pin fields: '{missing_str}' in pin at index {i}",
                file=sys.stderr
            )
            sys.exit(2)


def normalize_input(data: Dict[str, Any]) -> Dict[str, Any]:
    """
    Apply default values to optional fields.

    Args:
        data: Parsed JSON input.

    Returns:
        Input with defaults applied.
    """
    # Top-level defaults
    if "variant" not in data:
        data["variant"] = data["platform"]
    if "module" not in data:
        data["module"] = None
    if "wifi_enabled" not in data:
        data["wifi_enabled"] = False

    # Pin-level defaults
    for pin in data["pins"]:
        if "pull" not in pin:
            pin["pull"] = "none"
        if "device" not in pin:
            pin["device"] = "unknown"
        if "notes" not in pin:
            pin["notes"] = ""

    return data


def get_platform_instance(
    platform_str: str,
    variant: str,
    module: Optional[str]
) -> Any:
    """
    Get a platform instance, handling errors.

    Args:
        platform_str: Platform identifier.
        variant: Variant identifier.
        module: Module type (ESP32 only).

    Returns:
        Platform instance.

    Raises:
        SystemExit: On unknown platform (exit code 2).
    """
    try:
        return get_platform(platform_str, variant, module)
    except (ValueError, KeyError) as e:
        supported_list = ", ".join(SUPPORTED_PLATFORMS)
        print(
            f"Error: Unknown platform '{platform_str}'. "
            f"Supported platforms: {supported_list}",
            file=sys.stderr
        )
        sys.exit(2)


def is_rpi_platform(platform_str: str, variant: str) -> bool:
    """Check if platform is Raspberry Pi."""
    return (
        platform_str.lower().startswith("rpi") or
        platform_str.lower().startswith("raspberry") or
        variant.lower().startswith("rpi")
    )


def is_esp32_platform(platform_str: str, variant: str) -> bool:
    """Check if platform is ESP32."""
    return (
        platform_str.lower().startswith("esp") or
        variant.lower().startswith("esp")
    )


def determine_framework(
    cli_framework: Optional[str],
    data: Dict[str, Any],
    platform_str: str,
    variant: str
) -> str:
    """
    Determine the target framework.

    Priority:
    1. CLI --framework flag
    2. "framework" key in input JSON
    3. Platform default

    Args:
        cli_framework: Framework from CLI args (or None).
        data: Parsed input JSON.
        platform_str: Platform identifier.
        variant: Variant identifier.

    Returns:
        Framework string.

    Raises:
        SystemExit: On invalid or mismatched framework (exit code 2).
    """
    # Priority 1: CLI flag
    if cli_framework is not None:
        framework = cli_framework.lower()
    # Priority 2: JSON key
    elif "framework" in data:
        framework = data["framework"].lower()
    # Priority 3: Platform default
    elif is_rpi_platform(platform_str, variant):
        framework = "gpiozero"
    elif is_esp32_platform(platform_str, variant):
        framework = "arduino"
    else:
        print(
            f"Error: Cannot determine default framework for platform '{platform_str}'. "
            f"Use --framework to specify.",
            file=sys.stderr
        )
        sys.exit(2)

    # Validate framework is appropriate for platform
    if is_rpi_platform(platform_str, variant):
        if framework not in RPI_FRAMEWORKS:
            print(
                f"Error: Framework '{framework}' is not valid for Raspberry Pi. "
                f"Use one of: {', '.join(RPI_FRAMEWORKS)}",
                file=sys.stderr
            )
            sys.exit(2)
    elif is_esp32_platform(platform_str, variant):
        if framework not in ESP32_FRAMEWORKS:
            print(
                f"Error: Framework '{framework}' is not valid for ESP32. "
                f"Use one of: {', '.join(ESP32_FRAMEWORKS)}",
                file=sys.stderr
            )
            sys.exit(2)

    return framework


def format_validation_output(result: Any, output_format: str, verbose: bool) -> str:
    """
    Format validation result for output.

    Args:
        result: ValidationResult object.
        output_format: "json" or "text".
        verbose: Include additional detail.

    Returns:
        Formatted string.
    """
    if output_format == "json":
        return json.dumps(result.to_dict(), indent=2)

    # Text format
    lines = []
    status = "VALID" if result.valid else "INVALID"
    lines.append(f"Validation Result: {status}")
    lines.append("")

    if result.errors:
        lines.append(f"Errors ({len(result.errors)}):")
        for error in result.errors:
            gpio = error.get("gpio", "?")
            code = error.get("code", "UNKNOWN")
            message = error.get("message", "No message")
            if gpio == -1:
                lines.append(f"  [ERROR] {code} - {message}")
            else:
                lines.append(f"  [ERROR] GPIO{gpio}: {code} - {message}")
        lines.append("")

    if result.warnings:
        lines.append(f"Warnings ({len(result.warnings)}):")
        for warning in result.warnings:
            gpio = warning.get("gpio", "?")
            code = warning.get("code", "UNKNOWN")
            message = warning.get("message", "No message")
            if gpio == -1:
                lines.append(f"  [WARNING] {code} - {message}")
            else:
                lines.append(f"  [WARNING] GPIO{gpio}: {code} - {message}")
        lines.append("")

    if not result.errors and not result.warnings:
        lines.append("No errors or warnings.")
        lines.append("")

    lines.append("Summary:")
    summary = result.summary
    lines.append(f"  Total pins: {summary.get('total_pins', 0)}")
    lines.append(f"  Errors: {summary.get('errors', 0)}")
    lines.append(f"  Warnings: {summary.get('warnings', 0)}")
    lines.append(f"  Estimated current: {summary.get('current_draw_ma', 0.0)}mA")

    return "\n".join(lines)


def format_json_output(
    config_result: Any,
    init_code: str,
    validation_result: Any,
    framework: str,
    platform_str: str
) -> str:
    """
    Format complete generation result as JSON.

    Args:
        config_result: GenerationResult from generate_config().
        init_code: Code string from generate_code().
        validation_result: ValidationResult from validate().
        framework: Framework that was used.
        platform_str: Platform identifier.

    Returns:
        JSON string.
    """
    output = {
        "config_lines": config_result.config_lines,
        "init_code": init_code,
        "wiring_notes": config_result.wiring_notes,
        "warnings": config_result.warnings,
        "alternatives": config_result.alternatives,
        "framework": framework,
        "platform": platform_str,
        "validation": validation_result.to_dict()
    }
    return json.dumps(output, indent=2)


def format_text_output(
    config_result: Any,
    init_code: str,
    validation_result: Any,
    framework: str,
    platform_str: str,
    verbose: bool
) -> str:
    """
    Format complete generation result as human-readable text.

    Args:
        config_result: GenerationResult from generate_config().
        init_code: Code string from generate_code().
        validation_result: ValidationResult from validate().
        framework: Framework that was used.
        platform_str: Platform identifier.
        verbose: Include additional detail.

    Returns:
        Formatted text string.
    """
    lines = []

    lines.append(f"=== GPIO Configuration for {platform_str} ===")
    lines.append(f"Framework: {framework}")
    lines.append("")

    # Config lines
    lines.append("--- Config Lines ---")
    if config_result.config_lines:
        for line in config_result.config_lines:
            lines.append(line)
    else:
        lines.append("(none)")
    lines.append("")

    # Wiring notes
    lines.append("--- Wiring Notes ---")
    if config_result.wiring_notes:
        for note in config_result.wiring_notes:
            lines.append(note)
    else:
        lines.append("(none)")
    lines.append("")

    # Warnings
    lines.append("--- Warnings ---")
    if config_result.warnings:
        for warning in config_result.warnings:
            lines.append(f"  {warning}")
    else:
        lines.append("(none)")
    lines.append("")

    # Init code
    lines.append("--- Initialization Code ---")
    lines.append(init_code)
    lines.append("")

    # Validation summary
    lines.append("--- Validation ---")
    status = "VALID" if validation_result.valid else "INVALID"
    lines.append(f"Status: {status}")
    summary = validation_result.summary
    lines.append(
        f"Pins: {summary.get('total_pins', 0)}, "
        f"Errors: {summary.get('errors', 0)}, "
        f"Warnings: {summary.get('warnings', 0)}"
    )

    # Verbose: show validation details
    if verbose:
        if validation_result.errors:
            lines.append("")
            lines.append("Validation Errors:")
            for error in validation_result.errors:
                gpio = error.get("gpio", "?")
                message = error.get("message", "No message")
                lines.append(f"  GPIO{gpio}: {message}")

        if validation_result.warnings:
            lines.append("")
            lines.append("Validation Warnings:")
            for warning in validation_result.warnings:
                gpio = warning.get("gpio", "?")
                message = warning.get("message", "No message")
                lines.append(f"  GPIO{gpio}: {message}")

    return "\n".join(lines)


def main() -> None:
    """Main entry point for the generation script."""
    # Step 1: Parse CLI arguments
    args = parse_args()

    # Step 2: Read JSON input
    data = read_input(args.input_file)

    # Step 3: Validate JSON structure
    validate_input_structure(data)

    # Normalize with defaults
    data = normalize_input(data)

    # Extract key values
    platform_str = data["platform"]
    variant = data.get("variant", platform_str)
    module = data.get("module", None)

    # Step 4: Instantiate platform
    platform = get_platform_instance(platform_str, variant, module)

    # Step 5: Determine framework
    framework = determine_framework(args.framework, data, platform_str, variant)

    # Step 6: Run validation
    validation_result = platform.validate(data)

    # If validate-only mode, output and exit
    if args.validate_only:
        output = format_validation_output(validation_result, args.format, args.verbose)
        print(output)
        sys.exit(0 if validation_result.valid else 1)

    # Step 7: Run generation
    try:
        config_result = platform.generate_config(data)
        init_code = platform.generate_code(data, framework)
    except Exception as e:
        if args.verbose:
            import traceback
            traceback.print_exc()
        print(f"Error: Generation failed: {e}", file=sys.stderr)
        sys.exit(1)

    # Step 8: Build and output result
    if args.format == "json":
        output = format_json_output(
            config_result, init_code, validation_result, framework, platform_str
        )
    else:
        output = format_text_output(
            config_result, init_code, validation_result, framework, platform_str,
            args.verbose
        )

    print(output)

    # Step 9: Exit with success
    sys.exit(0)


if __name__ == "__main__":
    main()
