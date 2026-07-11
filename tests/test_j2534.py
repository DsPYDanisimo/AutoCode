"""Tests for J2534 protocol constants and error-code helpers."""

from j2534_passthru import J2534Proto, J2534Err


# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------

class TestJ2534Proto:
    def test_j1850vpw(self):
        assert J2534Proto.J1850VPW == 0x01

    def test_j1850pwm(self):
        assert J2534Proto.J1850PWM == 0x02

    def test_iso9141(self):
        assert J2534Proto.ISO9141 == 0x03

    def test_iso14230_kwp2000(self):
        assert J2534Proto.ISO14230 == 0x04

    def test_can(self):
        assert J2534Proto.CAN == 0x05

    def test_iso15765_obd_can(self):
        assert J2534Proto.ISO15765 == 0x06

    def test_all_constants_are_unique(self):
        values = [
            J2534Proto.J1850VPW, J2534Proto.J1850PWM,
            J2534Proto.ISO9141, J2534Proto.ISO14230,
            J2534Proto.CAN, J2534Proto.ISO15765,
        ]
        assert len(values) == len(set(values))

    def test_constants_are_positive_ints(self):
        for attr in ("J1850VPW", "J1850PWM", "ISO9141", "ISO14230", "CAN", "ISO15765"):
            val = getattr(J2534Proto, attr)
            assert isinstance(val, int) and val > 0, f"{attr} should be a positive int"


# ---------------------------------------------------------------------------
# Error constants
# ---------------------------------------------------------------------------

class TestJ2534ErrConstants:
    def test_status_noerror_is_zero(self):
        assert J2534Err.STATUS_NOERROR == 0x00

    def test_err_failed(self):
        assert J2534Err.ERR_FAILED == 0x07

    def test_err_device_not_connected(self):
        assert J2534Err.ERR_DEVICE_NOT_CONNECTED == 0x08

    def test_err_timeout(self):
        assert J2534Err.ERR_TIMEOUT == 0x09

    def test_err_buffer_empty(self):
        assert J2534Err.ERR_BUFFER_EMPTY == 0x10

    def test_err_buffer_full(self):
        assert J2534Err.ERR_BUFFER_FULL == 0x11


# ---------------------------------------------------------------------------
# J2534Err.name()
# ---------------------------------------------------------------------------

class TestJ2534ErrName:
    def test_status_noerror_name(self):
        assert J2534Err.name(0x00) == "STATUS_NOERROR"

    def test_err_failed_name(self):
        assert J2534Err.name(0x07) == "ERR_FAILED"

    def test_err_device_not_connected_name(self):
        assert J2534Err.name(0x08) == "ERR_DEVICE_NOT_CONNECTED"

    def test_err_timeout_name(self):
        assert J2534Err.name(0x09) == "ERR_TIMEOUT"

    def test_err_device_in_use_name(self):
        assert J2534Err.name(0x0E) == "ERR_DEVICE_IN_USE"

    def test_err_buffer_empty_name(self):
        assert J2534Err.name(0x10) == "ERR_BUFFER_EMPTY"

    def test_err_buffer_full_name(self):
        assert J2534Err.name(0x11) == "ERR_BUFFER_FULL"

    def test_err_invalid_baudrate_name(self):
        assert J2534Err.name(0x19) == "ERR_INVALID_BAUDRATE"

    def test_unknown_code_returns_hex_string(self):
        result = J2534Err.name(0xAB)
        assert "ERR_0x" in result
        assert "AB" in result.upper()

    def test_name_returns_string(self):
        for code in [0x00, 0x01, 0x07, 0x08, 0x09, 0x0E, 0x10, 0x11, 0x19]:
            assert isinstance(J2534Err.name(code), str)

    def test_all_defined_codes_return_non_hex_fallback(self):
        defined_codes = [0x00, 0x01, 0x07, 0x08, 0x09, 0x0E, 0x10, 0x11, 0x19]
        for code in defined_codes:
            name = J2534Err.name(code)
            assert not name.startswith("ERR_0x"), (
                f"Code 0x{code:02X} should have a named constant, got: {name}"
            )
