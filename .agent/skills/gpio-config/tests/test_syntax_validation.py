"""
Syntax validation tests for all 4 frameworks.

Ensures generated code is syntactically valid:
- Python: ast.parse() succeeds
- C/C++: Balanced braces
"""
import ast

import pytest


def validate_python_syntax(code):
    """Validate Python code syntax using ast.parse."""
    try:
        ast.parse(code)
        return True
    except SyntaxError:
        return False


def validate_cpp_braces(code):
    """Validate C/C++ code has balanced braces."""
    opens = code.count("{")
    closes = code.count("}")
    return opens == closes and opens > 0


# ===================================================================
# gpiozero framework
# ===================================================================

class TestGpiozeroSyntax:
    """gpiozero framework syntax validation."""

    @pytest.mark.parametrize("fixture_name", [
        "s1_rpi_i2c_spi.json",
        "s2_rpi_complex.json",
        "s3_rpi_can.json",
        "s4_rpi_uart_onewire.json",
        "s5b_rpi_shared_i2c.json",
        "s11a_rpi_bmesd.json",
    ])
    def test_gpiozero_syntax_valid(self, load_fixture, rpi4_platform, fixture_name):
        """All RPi fixtures should generate valid gpiozero Python."""
        assignment = load_fixture(fixture_name)
        assignment["platform"] = "rpi4"
        assignment["variant"] = "rpi4"
        code = rpi4_platform.generate_code(assignment, "gpiozero")
        assert code is not None
        assert len(code) > 0
        assert validate_python_syntax(code), f"Invalid Python in {fixture_name}"


# ===================================================================
# RPi.GPIO framework
# ===================================================================

class TestRpigpioSyntax:
    """RPi.GPIO framework syntax validation."""

    @pytest.mark.parametrize("fixture_name", [
        "s1_rpi_i2c_spi.json",
        "s4_rpi_uart_onewire.json",
        "s11a_rpi_bmesd.json",
    ])
    def test_rpigpio_syntax_valid(self, load_fixture, rpi4_platform, fixture_name):
        """Selected RPi fixtures should generate valid rpigpio Python."""
        assignment = load_fixture(fixture_name)
        assignment["platform"] = "rpi4"
        assignment["variant"] = "rpi4"
        code = rpi4_platform.generate_code(assignment, "rpigpio")
        assert code is not None
        assert len(code) > 0
        assert validate_python_syntax(code), f"Invalid Python in {fixture_name}"


# ===================================================================
# Arduino framework
# ===================================================================

class TestArduinoSyntax:
    """Arduino framework syntax validation."""

    @pytest.mark.parametrize("fixture_name", [
        "s6_esp32_i2c_spi.json",
        "s7a_esp32_adc1_wifi.json",
        "s8a_esp32_rtc_gpio.json",
        "s10_esp32_strapping.json",
        "s11b_esp32_bmesd.json",
    ])
    def test_arduino_syntax_valid_esp32(self, load_fixture, esp32_platform, fixture_name):
        """Base ESP32 fixtures should generate valid Arduino C++."""
        assignment = load_fixture(fixture_name)
        code = esp32_platform.generate_code(assignment, "arduino")
        assert code is not None
        assert len(code) > 0
        assert validate_cpp_braces(code), f"Unbalanced braces in {fixture_name}"

    def test_arduino_syntax_valid_esp32s3(self, load_fixture, esp32s3_platform):
        """ESP32-S3 multi fixture should generate valid Arduino C++."""
        assignment = load_fixture("s9_esp32s3_multi.json")
        code = esp32s3_platform.generate_code(assignment, "arduino")
        assert code is not None
        assert len(code) > 0
        assert validate_cpp_braces(code), "Unbalanced braces in s9_esp32s3_multi.json"


# ===================================================================
# ESP-IDF framework
# ===================================================================

class TestEspidfSyntax:
    """ESP-IDF framework syntax validation."""

    @pytest.mark.parametrize("fixture_name", [
        "s6_esp32_i2c_spi.json",
        "s8a_esp32_rtc_gpio.json",
    ])
    def test_espidf_syntax_valid(self, load_fixture, esp32_platform, fixture_name):
        """Selected ESP32 fixtures should generate valid ESP-IDF C."""
        assignment = load_fixture(fixture_name)
        code = esp32_platform.generate_code(assignment, "espidf")
        assert code is not None
        assert len(code) > 0
        assert validate_cpp_braces(code), f"Unbalanced braces in {fixture_name}"


# ===================================================================
# Meta: all 4 frameworks covered
# ===================================================================

class TestAllFrameworksCovered:
    """Meta-test to ensure all 4 frameworks have been exercised."""

    def test_gpiozero_tested(self, load_fixture, rpi4_platform):
        """gpiozero framework is available and tested."""
        assignment = load_fixture("s1_rpi_i2c_spi.json")
        code = rpi4_platform.generate_code(assignment, "gpiozero")
        assert "gpiozero" in code.lower() or "led" in code.lower() or "import" in code

    def test_rpigpio_tested(self, load_fixture, rpi4_platform):
        """rpigpio framework is available and tested."""
        assignment = load_fixture("s1_rpi_i2c_spi.json")
        code = rpi4_platform.generate_code(assignment, "rpigpio")
        assert "gpio" in code.lower()

    def test_arduino_tested(self, load_fixture, esp32_platform):
        """arduino framework is available and tested."""
        assignment = load_fixture("s6_esp32_i2c_spi.json")
        code = esp32_platform.generate_code(assignment, "arduino")
        assert "void" in code or "setup" in code.lower()

    def test_espidf_tested(self, load_fixture, esp32_platform):
        """espidf framework is available and tested."""
        assignment = load_fixture("s6_esp32_i2c_spi.json")
        code = esp32_platform.generate_code(assignment, "espidf")
        assert "gpio" in code.lower() or "esp" in code.lower()
