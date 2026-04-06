"""
Cross-platform scenario tests (S11) from Phase 8 integration testing.

Tests that the same logical request produces DIFFERENT but correct
pin assignments and configurations for each platform.
"""
import ast


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
# S11a: BME280 + SD Card on RPi 4
# ===================================================================

class TestS11aCrossPlatformRpi:
    """S11a: BME280 + SD Card on RPi 4."""

    def test_validation_passes(self, load_fixture, rpi4_platform, helpers):
        """S11a RPi should validate successfully."""
        assignment = load_fixture("s11a_rpi_bmesd.json")
        result = rpi4_platform.validate(assignment)
        helpers.assert_valid(result)

    def test_config_has_rpi_format(self, load_fixture, rpi4_platform):
        """S11a RPi config should use dtparam format."""
        assignment = load_fixture("s11a_rpi_bmesd.json")
        result = rpi4_platform.generate_config(assignment)
        config_text = " ".join(result.config_lines)
        assert "dtparam=i2c_arm=on" in config_text
        assert "dtparam=spi=on" in config_text

    def test_code_is_python(self, load_fixture, rpi4_platform):
        """S11a RPi code should be valid Python."""
        assignment = load_fixture("s11a_rpi_bmesd.json")
        code = rpi4_platform.generate_code(assignment, "gpiozero")
        assert validate_python_syntax(code)

    def test_no_esp32_contamination(self, load_fixture, rpi4_platform):
        """S11a RPi code should NOT contain ESP32 patterns."""
        assignment = load_fixture("s11a_rpi_bmesd.json")
        code = rpi4_platform.generate_code(assignment, "gpiozero")
        code_lower = code.lower()
        assert "pinmode" not in code_lower
        assert "gpio_config" not in code_lower
        assert "void setup" not in code_lower
        assert "arduino" not in code_lower


# ===================================================================
# S11b: BME280 + SD Card on ESP32
# ===================================================================

class TestS11bCrossPlatformEsp32:
    """S11b: BME280 + SD Card on ESP32."""

    def test_validation_passes(self, load_fixture, esp32_platform, helpers):
        """S11b ESP32 should validate successfully."""
        assignment = load_fixture("s11b_esp32_bmesd.json")
        result = esp32_platform.validate(assignment)
        helpers.assert_valid(result)

    def test_config_has_esp32_format(self, load_fixture, esp32_platform):
        """S11b ESP32 config should NOT use dtparam format."""
        assignment = load_fixture("s11b_esp32_bmesd.json")
        result = esp32_platform.generate_config(assignment)
        config_text = " ".join(result.config_lines)
        assert "dtparam" not in config_text
        assert "dtoverlay" not in config_text

    def test_code_is_cpp(self, load_fixture, esp32_platform):
        """S11b ESP32 code should be valid C++ (balanced braces)."""
        assignment = load_fixture("s11b_esp32_bmesd.json")
        code = esp32_platform.generate_code(assignment, "arduino")
        assert validate_cpp_braces(code)

    def test_no_rpi_contamination(self, load_fixture, esp32_platform):
        """S11b ESP32 code should NOT contain RPi patterns."""
        assignment = load_fixture("s11b_esp32_bmesd.json")
        code = esp32_platform.generate_code(assignment, "arduino")
        code_lower = code.lower()
        assert "gpiozero" not in code_lower
        assert "rpi.gpio" not in code_lower
        assert "pigpio" not in code_lower
        assert "dtparam" not in code_lower


# ===================================================================
# Cross-platform differentiation
# ===================================================================

class TestCrossPlatformDifferentiation:
    """Verify platforms produce appropriately different outputs."""

    def test_different_gpio_numbers(self, load_fixture):
        """Same request should use different GPIO numbers per platform."""
        rpi_assignment = load_fixture("s11a_rpi_bmesd.json")
        esp_assignment = load_fixture("s11b_esp32_bmesd.json")
        rpi_gpios = {p["gpio"] for p in rpi_assignment["pins"]}
        esp_gpios = {p["gpio"] for p in esp_assignment["pins"]}
        # RPi I2C uses GPIO2,3; ESP32 uses GPIO21,22
        assert 2 in rpi_gpios and 21 in esp_gpios
        assert rpi_gpios != esp_gpios

    def test_different_function_names(self, load_fixture):
        """Function names differ between platforms."""
        rpi_assignment = load_fixture("s11a_rpi_bmesd.json")
        esp_assignment = load_fixture("s11b_esp32_bmesd.json")
        rpi_functions = {p["function"] for p in rpi_assignment["pins"]}
        esp_functions = {p["function"] for p in esp_assignment["pins"]}
        # RPi uses SDA1/SCL1, ESP32 uses SDA/SCL
        assert "SDA1" in rpi_functions
        assert "SDA" in esp_functions

    def test_different_code_languages(self, load_fixture, rpi4_platform, esp32_platform):
        """RPi generates Python, ESP32 generates C++."""
        rpi_assignment = load_fixture("s11a_rpi_bmesd.json")
        esp_assignment = load_fixture("s11b_esp32_bmesd.json")
        rpi_code = rpi4_platform.generate_code(rpi_assignment, "gpiozero")
        esp_code = esp32_platform.generate_code(esp_assignment, "arduino")
        # Python has 'import' and 'def', C++ has 'void' and '#include'
        assert "import" in rpi_code or "def " in rpi_code
        assert "void" in esp_code or "#include" in esp_code
