from translations import get_tr, _TR


def test_get_tr_returns_dict():
    tr = get_tr()
    assert isinstance(tr, dict)


def test_get_tr_is_not_empty():
    assert len(get_tr()) > 0


def test_get_tr_lang_param_ignored():
    """Current implementation returns the same dict regardless of lang."""
    assert get_tr("ru") == get_tr("en") == get_tr("de")


def test_get_tr_returns_same_object_as_module_constant():
    assert get_tr() is _TR


def test_app_title_present_and_non_empty():
    assert get_tr().get("app_title", "") != ""


def test_connection_buttons_present():
    tr = get_tr()
    assert "btn_connect" in tr
    assert "btn_disconnect" in tr


def test_tab_keys_present():
    tr = get_tr()
    for key in ("tab_errors", "tab_live", "tab_info", "tab_calibration"):
        assert key in tr, f"Missing tab key: {key}"


def test_error_buttons_present():
    tr = get_tr()
    assert "btn_read_errors" in tr
    assert "btn_clear_errors" in tr


def test_settings_keys_present():
    tr = get_tr()
    for key in ("settings_title", "btn_save", "btn_cancel"):
        assert key in tr, f"Missing settings key: {key}"


def test_about_keys_present():
    tr = get_tr()
    for key in ("about_title", "about_version", "about_protocols"):
        assert key in tr, f"Missing about key: {key}"


def test_live_data_keys_present():
    tr = get_tr()
    for key in ("live_rpm", "live_speed", "live_coolant", "live_throttle", "live_voltage"):
        assert key in tr, f"Missing live-data key: {key}"


def test_dtc_column_keys_present():
    tr = get_tr()
    for key in ("col_code", "col_desc", "col_status", "col_source"):
        assert key in tr, f"Missing column key: {key}"


def test_no_ports_message_present():
    assert "no_ports" in get_tr()


def test_all_values_are_strings():
    for k, v in get_tr().items():
        assert isinstance(v, str), f"Value for '{k}' is not a string: {type(v)}"
