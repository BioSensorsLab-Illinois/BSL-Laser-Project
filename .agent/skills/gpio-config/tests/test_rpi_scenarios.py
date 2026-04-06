"""
RPi scenario tests (S1–S5) for gpio-config validation and code generation.

Covers:
  S1  – I2C BME280 + SPI SSD1306 on Pi 4
  S2  – Complex multi-protocol (I2C + SPI + PWM + GPIO)
  S3  – SPI-based CAN (MCP2515 + interrupt GPIO)
  S4  – UART GPS + 1-Wire DS18B20 + relay array
  S5a – Duplicate GPIO conflict (expected failure)
  S5b – Shared I2C bus (two devices, same SDA/SCL — valid)
"""

import ast


# ===================================================================
# S1: I2C BME280 + SPI SSD1306 on Pi 4
# ===================================================================

class TestS1RpiI2cSpi:
    """S1: I2C BME280 + SPI SSD1306 on Pi 4."""

    def test_s1_validate_valid(self, rpi4_platform, load_fixture, helpers):
        """S1-val: assignment is valid (0 errors)."""
        assignment = load_fixture("s1_rpi_i2c_spi.json")
        result = rpi4_platform.validate(assignment)
        helpers.assert_valid(result)

    def test_s1_missing_pullup_warning(self, rpi4_platform, load_fixture, helpers):
        """S1-pullup: I2C pins with pull='up' trigger MISSING_PULLUP warning."""
        assignment = load_fixture("s1_rpi_i2c_spi.json")
        result = rpi4_platform.validate(assignment)
        codes = helpers.get_warning_codes(result)
        assert "MISSING_PULLUP" in codes

    def test_s1_config_i2c_spi(self, rpi4_platform, load_fixture):
        """S1-cfg: config_lines contain dtparam for I2C and SPI."""
        assignment = load_fixture("s1_rpi_i2c_spi.json")
        gen = rpi4_platform.generate_config(assignment)
        joined = "\n".join(gen.config_lines)
        assert "dtparam=i2c_arm=on" in joined
        assert "dtparam=spi=on" in joined

    def test_s1_code_gpiozero_syntax(self, rpi4_platform, load_fixture):
        """S1-gpiozero: generated gpiozero code is valid Python."""
        assignment = load_fixture("s1_rpi_i2c_spi.json")
        code = rpi4_platform.generate_code(assignment, "gpiozero")
        ast.parse(code)  # raises SyntaxError on failure

    def test_s1_code_rpigpio_syntax(self, rpi4_platform, load_fixture):
        """S1-rpigpio: generated RPi.GPIO code is valid Python."""
        assignment = load_fixture("s1_rpi_i2c_spi.json")
        code = rpi4_platform.generate_code(assignment, "rpigpio")
        ast.parse(code)


# ===================================================================
# S2: Complex multi-protocol (I2C + SPI + PWM + GPIO)
# ===================================================================

class TestS2RpiComplex:
    """S2: I2C + SPI0 + PWM + GPIO outputs on Pi 4."""

    def test_s2_validate_valid(self, rpi4_platform, load_fixture, helpers):
        """S2-val: assignment is valid."""
        assignment = load_fixture("s2_rpi_complex.json")
        result = rpi4_platform.validate(assignment)
        helpers.assert_valid(result)

    def test_s2_missing_pullup_warning(self, rpi4_platform, load_fixture, helpers):
        """S2-pullup: I2C pins with pull='up' trigger MISSING_PULLUP."""
        assignment = load_fixture("s2_rpi_complex.json")
        result = rpi4_platform.validate(assignment)
        codes = helpers.get_warning_codes(result)
        assert "MISSING_PULLUP" in codes

    def test_s2_config_i2c_spi(self, rpi4_platform, load_fixture):
        """S2-cfg: config_lines include I2C and SPI enables."""
        assignment = load_fixture("s2_rpi_complex.json")
        gen = rpi4_platform.generate_config(assignment)
        joined = "\n".join(gen.config_lines)
        assert "dtparam=i2c_arm=on" in joined
        assert "dtparam=spi=on" in joined

    def test_s2_code_gpiozero_syntax(self, rpi4_platform, load_fixture):
        """S2-gpiozero: generated code is valid Python."""
        assignment = load_fixture("s2_rpi_complex.json")
        code = rpi4_platform.generate_code(assignment, "gpiozero")
        ast.parse(code)

    def test_s2_code_rpigpio_syntax(self, rpi4_platform, load_fixture):
        """S2-rpigpio: generated code is valid Python."""
        assignment = load_fixture("s2_rpi_complex.json")
        code = rpi4_platform.generate_code(assignment, "rpigpio")
        ast.parse(code)


# ===================================================================
# S3: SPI-based CAN (MCP2515 + interrupt GPIO)
# ===================================================================

class TestS3RpiCan:
    """S3: MCP2515 CAN controller over SPI0 + GPIO interrupt."""

    def test_s3_validate_valid(self, rpi4_platform, load_fixture, helpers):
        """S3-val: assignment is valid (no protocol-level errors)."""
        assignment = load_fixture("s3_rpi_can.json")
        result = rpi4_platform.validate(assignment)
        helpers.assert_valid(result)

    def test_s3_config_spi(self, rpi4_platform, load_fixture):
        """S3-cfg: config_lines enable SPI."""
        assignment = load_fixture("s3_rpi_can.json")
        gen = rpi4_platform.generate_config(assignment)
        joined = "\n".join(gen.config_lines)
        assert "dtparam=spi=on" in joined

    def test_s3_code_gpiozero_syntax(self, rpi4_platform, load_fixture):
        """S3-gpiozero: generated code is valid Python."""
        assignment = load_fixture("s3_rpi_can.json")
        code = rpi4_platform.generate_code(assignment, "gpiozero")
        ast.parse(code)

    def test_s3_code_rpigpio_syntax(self, rpi4_platform, load_fixture):
        """S3-rpigpio: generated code is valid Python."""
        assignment = load_fixture("s3_rpi_can.json")
        code = rpi4_platform.generate_code(assignment, "rpigpio")
        ast.parse(code)


# ===================================================================
# S4: UART GPS + 1-Wire DS18B20 + relay array
# ===================================================================

class TestS4RpiUartOnewire:
    """S4: UART GPS + 1-Wire DS18B20 + 4-relay array on Pi 4."""

    def test_s4_validate_valid(self, rpi4_platform, load_fixture, helpers):
        """S4-val: assignment is valid."""
        assignment = load_fixture("s4_rpi_uart_onewire.json")
        result = rpi4_platform.validate(assignment)
        helpers.assert_valid(result)

    def test_s4_uart_bt_conflict_warning(self, rpi4_platform, load_fixture, helpers):
        """S4-uart: GPIO14/15 on rpi4 produce UART_BT_CONFLICT warning."""
        assignment = load_fixture("s4_rpi_uart_onewire.json")
        result = rpi4_platform.validate(assignment)
        codes = helpers.get_warning_codes(result)
        assert "UART_BT_CONFLICT" in codes

    def test_s4_missing_pullup_warning(self, rpi4_platform, load_fixture, helpers):
        """S4-pullup: 1-Wire pin with pull='up' triggers MISSING_PULLUP."""
        assignment = load_fixture("s4_rpi_uart_onewire.json")
        result = rpi4_platform.validate(assignment)
        codes = helpers.get_warning_codes(result)
        assert "MISSING_PULLUP" in codes

    def test_s4_config_uart_onewire(self, rpi4_platform, load_fixture):
        """S4-cfg: config_lines include UART enable and 1-Wire overlay."""
        assignment = load_fixture("s4_rpi_uart_onewire.json")
        gen = rpi4_platform.generate_config(assignment)
        joined = "\n".join(gen.config_lines)
        assert "enable_uart=1" in joined
        assert "dtoverlay=w1-gpio" in joined

    def test_s4_code_gpiozero_syntax(self, rpi4_platform, load_fixture):
        """S4-gpiozero: generated code is valid Python."""
        assignment = load_fixture("s4_rpi_uart_onewire.json")
        code = rpi4_platform.generate_code(assignment, "gpiozero")
        ast.parse(code)

    def test_s4_code_rpigpio_syntax(self, rpi4_platform, load_fixture):
        """S4-rpigpio: generated code is valid Python."""
        assignment = load_fixture("s4_rpi_uart_onewire.json")
        code = rpi4_platform.generate_code(assignment, "rpigpio")
        ast.parse(code)


# ===================================================================
# S5a: Duplicate GPIO conflict (expected failure)
# ===================================================================

class TestS5aDuplicateConflict:
    """S5a: GPIO17 assigned twice — must produce DIRECT_CONFLICT error."""

    def test_s5a_validate_invalid(self, rpi4_platform, load_fixture, helpers):
        """S5a-val: assignment is invalid."""
        assignment = load_fixture("s5a_rpi_duplicate_conflict.json")
        result = rpi4_platform.validate(assignment)
        helpers.assert_invalid(result)

    def test_s5a_direct_conflict_error(self, rpi4_platform, load_fixture, helpers):
        """S5a-code: errors contain DIRECT_CONFLICT."""
        assignment = load_fixture("s5a_rpi_duplicate_conflict.json")
        result = rpi4_platform.validate(assignment)
        codes = helpers.get_error_codes(result)
        assert "DIRECT_CONFLICT" in codes


# ===================================================================
# S5b: Shared I2C bus (two devices, same SDA/SCL — valid)
# ===================================================================

class TestS5bSharedI2c:
    """S5b: Two I2C devices sharing SDA/SCL — single pin-pair entry."""

    def test_s5b_validate_valid(self, rpi4_platform, load_fixture, helpers):
        """S5b-val: shared bus is valid (no duplicate GPIOs)."""
        assignment = load_fixture("s5b_rpi_shared_i2c.json")
        result = rpi4_platform.validate(assignment)
        helpers.assert_valid(result)

    def test_s5b_missing_pullup_warning(self, rpi4_platform, load_fixture, helpers):
        """S5b-pullup: I2C pins with pull='up' trigger MISSING_PULLUP."""
        assignment = load_fixture("s5b_rpi_shared_i2c.json")
        result = rpi4_platform.validate(assignment)
        codes = helpers.get_warning_codes(result)
        assert "MISSING_PULLUP" in codes
