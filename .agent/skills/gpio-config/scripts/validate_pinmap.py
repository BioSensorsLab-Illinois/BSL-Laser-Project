#!/usr/bin/env python3
"""GPIO pin map validation script.

Reads a JSON pin assignment, validates it against platform-specific rules,
and outputs validation results.

Usage:
    echo '{"platform":"rpi4",...}' | python validate_pinmap.py
    python validate_pinmap.py input.json
    python validate_pinmap.py --format text input.json
    python validate_pinmap.py --help
"""

import argparse
import json
import sys
from typing import Any, Dict, List, Optional

from platforms import get_platform


SUPPORTED_PLATFORMS = [
    "rpi3", "rpi4", "rpi5", "rpi_zero2w",
    "esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"
]


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Validate GPIO pin assignments against platform-specific rules."
    )
    parser.add_argument(
        "input_file",
        nargs="?",
        default=None,
        help="JSON input file. Reads from stdin if not provided."
    )
    parser.add_argument(
        "--format",
        choices=["json", "text"],
        default="json",
        help="Output format (default: json)."
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Include additional detail in messages."
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
        if input_file is not None:
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


def validate_input_structure(data: Dict[str, Any]) -> None:
    """
    Validate that the input JSON has required fields.

    Args:
        data: Parsed JSON input.

    Raises:
        SystemExit: On missing required fields (exit code 2).
    """
    # Check top-level required fields
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


def format_json_output(result: Any) -> str:
    """
    Format validation result as JSON.

    Args:
        result: ValidationResult object.

    Returns:
        JSON string.
    """
    return json.dumps(result.to_dict(), indent=2)


def format_text_output(result: Any, verbose: bool = False) -> str:
    """
    Format validation result as human-readable text.

    Args:
        result: ValidationResult object.
        verbose: Include additional detail.

    Returns:
        Formatted text string.
    """
    lines = []

    # Validation status
    status = "VALID" if result.valid else "INVALID"
    lines.append(f"Validation Result: {status}")
    lines.append("")

    # Errors
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

    # Warnings
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

    # No issues message
    if not result.errors and not result.warnings:
        lines.append("No errors or warnings.")
        lines.append("")

    # Summary
    lines.append("Summary:")
    summary = result.summary
    lines.append(f"  Total pins: {summary.get('total_pins', 0)}")
    lines.append(f"  Errors: {summary.get('errors', 0)}")
    lines.append(f"  Warnings: {summary.get('warnings', 0)}")
    lines.append(f"  Estimated current: {summary.get('current_draw_ma', 0.0)}mA")

    return "\n".join(lines)


def main() -> None:
    """Main entry point for the validation script."""
    # Step 1: Parse arguments
    args = parse_args()

    # Step 2: Read JSON input
    data = read_input(args.input_file)

    # Step 3: Validate JSON structure
    validate_input_structure(data)

    # Normalize with defaults
    data = normalize_input(data)

    # Step 4: Get platform instance
    platform_str = data["platform"]
    variant = data.get("variant", platform_str)
    module = data.get("module", None)
    platform = get_platform_instance(platform_str, variant, module)

    # Step 5: Run validation
    result = platform.validate(data)

    # Step 6: Output results
    if args.format == "json":
        output = format_json_output(result)
    else:
        output = format_text_output(result, verbose=args.verbose)

    print(output)

    # Step 7: Exit code
    sys.exit(0 if result.valid else 1)


if __name__ == "__main__":
    main()
