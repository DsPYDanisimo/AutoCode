_TR_RU = {
    "app_title": "Диагностика и калибровка ЭБУ автомобилей v1.0",
    "lbl_type": "Тип:", "lbl_device": "Устройство:",
    "lbl_protocol": "Протокол:", "lbl_baud": "Скорость (бод):",
    "btn_connect": "🔌 Подключиться", "btn_disconnect": "⛔ Отключиться",
    "tt_refresh_ports": "Обновить список портов",
    "tt_bt_scan": "Поиск Bluetooth-адаптеров",
    "tt_j2534_scan": "Поиск J2534/PassThru адаптеров в реестре Windows",
    "tt_settings": "Настройки",
    "port_all": "Все",
    "proto_auto": "Автоопределение",
    "tab_errors": "Коды ошибок", "tab_live": "Live данные",
    "tab_info": "Информация ЭБУ", "tab_calibration": "Калибровка",
    "btn_read_errors": "📋 Считать ошибки",
    "btn_clear_errors": "🗑️ Стереть ошибки",
    "btn_dtc_db": "📚 База DTC",
    "lbl_status": "Статус:", "lbl_system": "Система:", "lbl_search": "Поиск:",
    "ph_search": "Код или описание…", "no_data": "Нет данных",
    "col_code": "Код", "col_desc": "Описание", "col_status": "Статус",
    "col_source": "Источник", "col_system": "Система", "col_severity": "Тяжесть",
    "btn_start_live": "▶️ Запустить мониторинг", "btn_stop_live": "⏹️ Остановить",
    "live_rpm": "Обороты двигателя", "live_speed": "Скорость",
    "live_coolant": "Температура ОЖ", "live_throttle": "Положение дросселя",
    "live_voltage": "Напряжение", "live_maf": "Расход воздуха",
    "live_intake": "Температура впуска", "live_fuel_pressure": "Давление топлива",
    "btn_refresh_info": "🔄 Обновить информацию",
    "grp_calibration": "Калибровка параметров (Stage 1)",
    "lbl_active_cal": "Активная калибровка:",
    "cal_not_loaded": "⚠️  Не загружена — считайте калибровку с ЭБУ",
    "btn_read_ecu": "📥 Считать с ЭБУ", "btn_load_backup": "📂 Из бэкапа",
    "btn_ignition": "⏱️ Угол зажигания", "btn_fuel_map": "⛽ Топливная карта",
    "btn_boost": "💨 Управление турбиной", "btn_rev_limit": "⏫ Ограничитель оборотов",
    "btn_write_ecu": "✍️ Записать в ЭБУ",
    "btn_reset_changes": "↩️ Сбросить изменения",
    "btn_restore_backup": "🔄 Восстановить из бэкапа",
    "grp_log": "Лог событий",
    "btn_help": "❓ Справка", "btn_export_log": "📤 Экспорт логов",
    "no_ports": "Нет доступных портов",
    "dtc_shown": "Показано: {shown} из {total}", "dtc_total": "Всего: {total}",
    "settings_title": "Настройки",
    "tab_conn": "🔌 Подключение", "tab_diag": "🔧 Диагностика",
    "tab_ui": "🖥️ Интерфейс", "tab_about": "ℹ️ О программе",
    "btn_save": "💾 Сохранить", "btn_cancel": "Отмена",
    "set_remember_port": "Запоминать последний порт:",
    "set_auto_reconnect": "Авто-переподключение при разрыве:",
    "set_reconnect_attempts": "Количество попыток:",
    "set_timeout": "Таймаут подключения:",
    "set_default_protocol": "Протокол по умолчанию:",
    "set_default_baud": "Скорость по умолчанию:",
    "suffix_attempts": " попыток", "suffix_sec": " сек",
    "set_auto_read_dtc": "Авто-считывание DTC при подключении:",
    "set_show_pending": "Показывать ожидающие коды (Pending):",
    "set_dtc_limit": "Максимум DTC в таблице:",
    "set_live_interval": "Интервал Live данных:",
    "set_quality_interval": "Интервал замера качества сигнала:",
    "suffix_codes": " кодов", "suffix_ms": " мс",
    "set_log_lines": "Максимум строк в логе:",
    "set_autoscroll": "Авто-прокрутка лога:",
    "set_timestamps": "Временные метки в логе:",
    "set_log_level": "Уровень логирования:",
    "set_confirm_clear": "Подтверждение при стирании DTC:",
    "set_confirm_write": "Подтверждение при записи калибровки:",
    "set_language": "Язык интерфейса:",
    "suffix_lines": " строк",
    "about_title": "Диагностика и калибровка ЭБУ",
    "about_version": "Версия", "about_protocols": "Протоколы",
    "about_interface": "Интерфейс", "about_dtc_db": "База DTC",
    "about_warning": (
        "⚠️ Запись калибровки в ЭБУ необратима.\n"
        "Используйте только при наличии резервной копии прошивки."
    ),
    "tab_docs": "📖 Документация",
    "restart_required_title": "Требуется перезапуск",
    "restart_required_body": "Смена языка интерфейса вступит в силу после перезапуска программы.",
    "simulated_badge": "⚠️ Демо-режим (не подключено к реальному ЭБУ)",
    "confirm_word": "ПОДТВЕРЖДАЮ",
    "confirm_word_hint": "Чтобы продолжить, введите слово «{word}»:",
    "confirm_word_mismatch": "Слово введено неверно — действие отменено.",
    "write_ecu_title": "Запись в ЭБУ — необратимое действие",
    "write_ecu_body": (
        "Запись калибровки в ЭБУ может привести к:\n"
        "• Потере гарантии\n"
        "• Повреждению двигателя\n"
        "• Неисправности автомобиля\n\n"
        "Демо-режим: реальная запись в ЭБУ в этой версии не подключена — "
        "изменения будут сохранены только в редакторе."
    ),
    "write_ecu_done": (
        "✅ Изменения сохранены в редакторе.\n"
        "⚠️ Демо-режим: реальная запись в ЭБУ ещё не реализована — "
        "данные не передавались на автомобиль."
    ),
    "restore_backup_title": "Восстановление прошивки ЭБУ",
    "restore_backup_body": (
        "Восстановить активную калибровку из резервной копии? Текущие несохранённые изменения будут потеряны.\n\n"
        "Демо-режим: реальное восстановление прошивки ЭБУ в этой версии не подключено."
    ),
    "restore_backup_done": (
        "✅ Активная калибровка заменена данными из резервной копии.\n"
        "⚠️ Демо-режим: реальное восстановление прошивки ЭБУ ещё не реализовано."
    ),
}

_TR_EN = {
    "app_title": "ECU Diagnostics & Calibration Suite v1.0",
    "lbl_type": "Type:", "lbl_device": "Device:",
    "lbl_protocol": "Protocol:", "lbl_baud": "Baud rate:",
    "btn_connect": "🔌 Connect", "btn_disconnect": "⛔ Disconnect",
    "tt_refresh_ports": "Refresh port list",
    "tt_bt_scan": "Scan for Bluetooth adapters",
    "tt_j2534_scan": "Scan Windows registry for J2534/PassThru adapters",
    "tt_settings": "Settings",
    "port_all": "All",
    "proto_auto": "Auto-detect",
    "tab_errors": "Trouble Codes", "tab_live": "Live Data",
    "tab_info": "ECU Info", "tab_calibration": "Calibration",
    "btn_read_errors": "📋 Read Codes",
    "btn_clear_errors": "🗑️ Clear Codes",
    "btn_dtc_db": "📚 DTC Database",
    "lbl_status": "Status:", "lbl_system": "System:", "lbl_search": "Search:",
    "ph_search": "Code or description…", "no_data": "No data",
    "col_code": "Code", "col_desc": "Description", "col_status": "Status",
    "col_source": "Source", "col_system": "System", "col_severity": "Severity",
    "btn_start_live": "▶️ Start Monitoring", "btn_stop_live": "⏹️ Stop",
    "live_rpm": "Engine RPM", "live_speed": "Speed",
    "live_coolant": "Coolant Temp", "live_throttle": "Throttle Position",
    "live_voltage": "Voltage", "live_maf": "Mass Air Flow",
    "live_intake": "Intake Air Temp", "live_fuel_pressure": "Fuel Pressure",
    "btn_refresh_info": "🔄 Refresh Info",
    "grp_calibration": "Parameter Calibration (Stage 1)",
    "lbl_active_cal": "Active calibration:",
    "cal_not_loaded": "⚠️  Not loaded — read a calibration from the ECU first",
    "btn_read_ecu": "📥 Read from ECU", "btn_load_backup": "📂 From Backup",
    "btn_ignition": "⏱️ Ignition Timing", "btn_fuel_map": "⛽ Fuel Map",
    "btn_boost": "💨 Boost Control", "btn_rev_limit": "⏫ Rev Limiter",
    "btn_write_ecu": "✍️ Write to ECU",
    "btn_reset_changes": "↩️ Reset Changes",
    "btn_restore_backup": "🔄 Restore from Backup",
    "grp_log": "Event Log",
    "btn_help": "❓ Help", "btn_export_log": "📤 Export Log",
    "no_ports": "No ports available",
    "dtc_shown": "Shown: {shown} of {total}", "dtc_total": "Total: {total}",
    "settings_title": "Settings",
    "tab_conn": "🔌 Connection", "tab_diag": "🔧 Diagnostics",
    "tab_ui": "🖥️ Interface", "tab_about": "ℹ️ About",
    "btn_save": "💾 Save", "btn_cancel": "Cancel",
    "set_remember_port": "Remember last port:",
    "set_auto_reconnect": "Auto-reconnect on disconnect:",
    "set_reconnect_attempts": "Number of attempts:",
    "set_timeout": "Connection timeout:",
    "set_default_protocol": "Default protocol:",
    "set_default_baud": "Default baud rate:",
    "suffix_attempts": " attempts", "suffix_sec": " sec",
    "set_auto_read_dtc": "Auto-read DTCs on connect:",
    "set_show_pending": "Show pending codes:",
    "set_dtc_limit": "Max DTCs in table:",
    "set_live_interval": "Live data interval:",
    "set_quality_interval": "Signal quality poll interval:",
    "suffix_codes": " codes", "suffix_ms": " ms",
    "set_log_lines": "Max log lines:",
    "set_autoscroll": "Auto-scroll log:",
    "set_timestamps": "Timestamps in log:",
    "set_log_level": "Log level:",
    "set_confirm_clear": "Confirm before clearing DTCs:",
    "set_confirm_write": "Confirm before writing calibration:",
    "set_language": "Interface language:",
    "suffix_lines": " lines",
    "about_title": "ECU Diagnostics & Calibration",
    "about_version": "Version", "about_protocols": "Protocols",
    "about_interface": "Interface", "about_dtc_db": "DTC Database",
    "about_warning": (
        "⚠️ Writing a calibration to the ECU is irreversible.\n"
        "Only proceed if you have a firmware backup."
    ),
    "tab_docs": "📖 Documentation",
    "restart_required_title": "Restart Required",
    "restart_required_body": "The interface language will change after the application restarts.",
    "simulated_badge": "⚠️ Demo mode (not connected to a real ECU)",
    "confirm_word": "CONFIRM",
    "confirm_word_hint": "Type “{word}” to continue:",
    "confirm_word_mismatch": "The word didn't match — action cancelled.",
    "write_ecu_title": "Write to ECU — irreversible action",
    "write_ecu_body": (
        "Writing a calibration to the ECU can lead to:\n"
        "• Loss of warranty\n"
        "• Engine damage\n"
        "• Vehicle malfunction\n\n"
        "Demo mode: real ECU writing isn't wired up in this build — "
        "changes will only be saved in the editor."
    ),
    "write_ecu_done": (
        "✅ Changes saved in the editor.\n"
        "⚠️ Demo mode: real ECU writing isn't implemented yet — "
        "no data was sent to the vehicle."
    ),
    "restore_backup_title": "Restore ECU Firmware",
    "restore_backup_body": (
        "Restore the active calibration from a backup? Unsaved changes will be lost.\n\n"
        "Demo mode: real ECU firmware restore isn't wired up in this build."
    ),
    "restore_backup_done": (
        "✅ Active calibration replaced with the backup data.\n"
        "⚠️ Demo mode: real ECU firmware restore isn't implemented yet."
    ),
}

_LANGUAGES = {"ru": _TR_RU, "en": _TR_EN}


def get_tr(lang: str = "ru") -> dict:
    return _LANGUAGES.get(lang, _TR_RU)
