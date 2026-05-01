"""
j2534_passthru.py — J2534 (PassThru) интерфейс для Python

Работает с любым адаптером, зарегистрированным как PassThru устройство в
реестре Windows (MongoosePro, OBDLink SX/MX, ELS27, Mongoose-IHU и др.).

Архитектура:
  scan_j2534_registry()  — находит устройства через HKLM\\PassThruSupport.04.04
  J2534PassThru          — обёртка над PassThru DLL через ctypes
"""

import ctypes
import json as _json
import logging
import os as _os
import sys
import time
from typing import List, Optional

logger = logging.getLogger(__name__)

# ─── Доступность только на Windows ────────────────────────────────────────────
if sys.platform != "win32":
    def scan_j2534_registry() -> list:
        return []
    class J2534PassThru:
        def __init__(self, *a, **kw): pass
        def open(self): return False
        def is_connected(self): return False
        def disconnect(self): pass
else:
    import winreg

# ─── Протоколы PassThru ────────────────────────────────────────────────────────
class J2534Proto:
    J1850VPW = 0x01
    J1850PWM = 0x02
    ISO9141  = 0x03
    ISO14230 = 0x04   # KWP2000
    CAN      = 0x05
    ISO15765 = 0x06   # CAN-OBD-II (самый распространённый)

# ─── Коды ошибок ──────────────────────────────────────────────────────────────
class J2534Err:
    STATUS_NOERROR        = 0x00
    ERR_FAILED            = 0x07
    ERR_DEVICE_NOT_CONNECTED = 0x08
    ERR_TIMEOUT           = 0x09
    ERR_BUFFER_EMPTY      = 0x10
    ERR_BUFFER_FULL       = 0x11

    _NAMES = {
        0x00: "STATUS_NOERROR",
        0x01: "ERR_NOT_SUPPORTED",
        0x07: "ERR_FAILED",
        0x08: "ERR_DEVICE_NOT_CONNECTED",
        0x09: "ERR_TIMEOUT",
        0x0E: "ERR_DEVICE_IN_USE",
        0x10: "ERR_BUFFER_EMPTY",
        0x11: "ERR_BUFFER_FULL",
        0x19: "ERR_INVALID_BAUDRATE",
    }

    @classmethod
    def name(cls, code: int) -> str:
        return cls._NAMES.get(code, f"ERR_0x{code:02X}")

# ─── Структура PassThru сообщения ─────────────────────────────────────────────
_MAX_MSG = 4128

class PASSTHRU_MSG(ctypes.Structure):
    _fields_ = [
        ("ProtocolID",     ctypes.c_ulong),
        ("RxStatus",       ctypes.c_ulong),
        ("TxFlags",        ctypes.c_ulong),
        ("Timestamp",      ctypes.c_ulong),
        ("DataSize",       ctypes.c_ulong),
        ("ExtraDataIndex", ctypes.c_ulong),
        ("Data",           ctypes.c_ubyte * _MAX_MSG),
    ]

# ─── Сканирование реестра ──────────────────────────────────────────────────────

_REG_PATHS = [
    r"SOFTWARE\PassThruSupport.04.04",
    r"SOFTWARE\WOW6432Node\PassThruSupport.04.04",
]


def scan_j2534_registry() -> List[dict]:
    """Возвращает список J2534 устройств, найденных в реестре Windows."""
    if sys.platform != "win32":
        return []

    devices: List[dict] = []

    for reg_path in _REG_PATHS:
        try:
            root = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, reg_path)
        except FileNotFoundError:
            continue

        i = 0
        while True:
            try:
                dev_name = winreg.EnumKey(root, i)
                i += 1
            except OSError:
                break

            try:
                dev_key  = winreg.OpenKey(root, dev_name)
                dll_path = winreg.QueryValueEx(dev_key, "FunctionLibrary")[0]

                try:
                    vendor = winreg.QueryValueEx(dev_key, "Vendor")[0]
                except OSError:
                    vendor = dev_name

                devices.append({
                    "name":        f"J2534:{dev_name}",
                    "description": f"{vendor} (J2534/PassThru)",
                    "type":        "j2534",
                    "dll_path":    dll_path,
                    "device_name": dev_name,
                    "vendor":      vendor,
                    "paired":      True,
                })
                logger.info(f"J2534: найдено '{dev_name}' → {dll_path}")
            except Exception as e:
                logger.debug(f"J2534: пропускаем '{dev_name}': {e}")

        winreg.CloseKey(root)

    # Убираем дубликаты по dll_path
    seen: set = set()
    unique = []
    for d in devices:
        key = d["dll_path"].lower()
        if key not in seen:
            seen.add(key)
            unique.append(d)

    return unique


# ─── PassThru DLL обёртка ──────────────────────────────────────────────────────

# Протоколы для автоопределения (от наиболее вероятного к менее).
# Порядок важен: быстрее всего должны находить современные CAN-автомобили.
_AUTO_PROTOS = [
    # ── ISO 15765 / CAN OBD-II ────────────────────────────────────────────────
    # 500 кбит — большинство авто с 2008: JLR, BMW, Mercedes, Toyota, Honda (новые),
    #            Kia, Hyundai, Renault, Peugeot, GM (USA 2010+), Ford (USA 2008+)
    (J2534Proto.ISO15765, 500000),
    # 250 кбит — Ford Focus/Fiesta/Mondeo до 2015, VW/Audi/Skoda/Seat (HS-CAN),
    #            некоторые GM, большинство дизельных грузовиков (SAE J1939)
    (J2534Proto.ISO15765, 250000),
    # 125 кбит — Honda/Acura 2003–2008 (старый CAN), Mitsubishi до 2007,
    #            некоторые Mazda/Suzuki, Subaru CANBUS pre-2008
    (J2534Proto.ISO15765, 125000),
    # 1 Мбит — BMW F/G-кузов (2012+), Mercedes W222/W213/W205 (CAN FD, обратная совместимость),
    #          Audi/VW MQB 2016+ (некоторые шины), новые Jaguar (инфотейнмент)
    (J2534Proto.ISO15765, 1000000),
    # 33.3 кбит — GM Single Wire CAN (SWCAN/J2411): некоторые Chevrolet/GMC/Buick 2004–2010
    #              Предупреждение: MongoosePro JLR может не поддерживать нестандартную скорость
    (J2534Proto.ISO15765, 33333),
    # ── ISO 14230 / KWP2000 (K-Line) ─────────────────────────────────────────
    # 10 400 бод, Fast-Init — VW/Audi/Skoda до 2006, BMW E46/E39/E53 до 2004,
    #                         Opel/Vauxhall, Peugeot/Citroën до 2006, Renault до 2007
    (J2534Proto.ISO14230, 10400),
    # 5 000 бод, Slow-Init — Mercedes W210/W202/W203 (pre-2004), некоторые Chrysler/Jeep
    (J2534Proto.ISO14230, 5000),
    # ── ISO 9141-2 (K-Line, старый OBD-II) ───────────────────────────────────
    # 10 400 бод — авто 1996–2002 (Chrysler/Dodge/Jeep, Honda/Acura до 2003,
    #              Toyota/Lexus до 2002, BMW E36/E39 ранние, Volvo до 2002)
    (J2534Proto.ISO9141,  10400),
    # 4 800 бод — очень старые авто (до 1996, некоторые Audi/VW ISO 9141-1)
    (J2534Proto.ISO9141,  4800),
]

# TxFlags для ISO15765 (frame padding)
_ISO15765_PAD = 0x40


class J2534PassThru:
    """Тонкая обёртка над J2534 PassThru DLL."""

    def __init__(self, dll_path: str):
        self._dll_path   = dll_path
        self._dll        = None        # ctypes.WinDLL
        self._device_id  = ctypes.c_ulong(0)
        self._channel_id = ctypes.c_ulong(0)
        self._protocol   = J2534Proto.ISO15765
        self._is_can     = True
        self._connected  = False

    # ── Внутренние ────────────────────────────────────────────────────────────

    def _dll_load(self):
        if self._dll is None:
            self._dll = ctypes.WinDLL(self._dll_path)
        return self._dll

    def _ret_ok(self, ret: int, op: str) -> bool:
        if ret == J2534Err.STATUS_NOERROR:
            return True
        logger.warning(f"J2534 {op}: {J2534Err.name(ret)} (0x{ret:02X})")
        return False

    # ── Открытие / закрытие устройства ────────────────────────────────────────

    def open(self) -> bool:
        try:
            dll = self._dll_load()
            ret = dll.PassThruOpen(None, ctypes.byref(self._device_id))
            if not self._ret_ok(ret, "PassThruOpen"):
                return False
            logger.info(f"J2534 открыт: DeviceID={self._device_id.value}")
            return True
        except Exception as e:
            logger.error(f"J2534 open exception: {e}")
            return False

    def close(self):
        try:
            if self._dll:
                self._dll.PassThruClose(self._device_id)
        except Exception as e:
            logger.debug(f"J2534 close: {e}")

    # ── Подключение к шине ────────────────────────────────────────────────────

    def connect(self, protocol: int = J2534Proto.ISO15765,
                flags: int = 0, baud_rate: int = 500000) -> bool:
        try:
            dll = self._dll_load()
            ret = dll.PassThruConnect(
                self._device_id,
                ctypes.c_ulong(protocol),
                ctypes.c_ulong(flags),
                ctypes.c_ulong(baud_rate),
                ctypes.byref(self._channel_id),
            )
            if not self._ret_ok(ret, "PassThruConnect"):
                return False
            self._protocol  = protocol
            self._is_can    = protocol in (J2534Proto.ISO15765, J2534Proto.CAN)
            self._connected = True
            logger.info(f"J2534 канал: ChannelID={self._channel_id.value}, "
                        f"proto={protocol}, baud={baud_rate}")
            return True
        except Exception as e:
            logger.error(f"J2534 connect exception: {e}")
            return False

    def auto_connect(self) -> bool:
        """Пробует протоколы по очереди до первого успеха.
        Нестандартные скорости (SWCAN 33333 бод) пробуются отдельно с перехватом
        исключения — некоторые адаптеры отклоняют их на уровне DLL."""
        _NONSTANDARD = {33333}
        for proto, baud in _AUTO_PROTOS:
            try:
                if self.connect(protocol=proto, baud_rate=baud):
                    return True
            except Exception as e:
                if baud in _NONSTANDARD:
                    logger.debug(f"auto_connect: пропуск {proto}/{baud} ({e})")
                else:
                    raise
        return False

    def disconnect_channel(self):
        try:
            if self._connected and self._dll:
                self._dll.PassThruDisconnect(self._channel_id)
                self._connected = False
        except Exception as e:
            logger.debug(f"J2534 disconnect_channel: {e}")

    def disconnect(self):
        self.disconnect_channel()
        self.close()
        self._dll = None

    # ── Фильтр OBD-II ответов ─────────────────────────────────────────────────

    def _make_filter_msg(self, can_id: int, with_pad: bool = False) -> "PASSTHRU_MSG":
        """Создаёт PASSTHRU_MSG для использования в PassThruStartMsgFilter."""
        m = PASSTHRU_MSG()
        m.ProtocolID = self._protocol
        m.TxFlags    = _ISO15765_PAD if with_pad else 0
        m.DataSize   = 4
        m.Data[0] = (can_id >> 24) & 0xFF
        m.Data[1] = (can_id >> 16) & 0xFF
        m.Data[2] = (can_id >>  8) & 0xFF
        m.Data[3] =  can_id        & 0xFF
        return m

    def _add_flow_filter(self, mask_id: int, pattern_id: int,
                         flow_id: int, label: str = "") -> int:
        """
        Добавляет FLOW_CONTROL_FILTER и возвращает FilterID (>0) или 0 при ошибке.
        flow-сообщение отправляется с ISO15765_PAD чтобы FC-фреймы
        соответствовали padding наших UDS-запросов.
        """
        try:
            dll = self._dll_load()
            fid = ctypes.c_ulong(0)
            ret = dll.PassThruStartMsgFilter(
                self._channel_id,
                ctypes.c_ulong(0x03),   # FLOW_CONTROL_FILTER
                ctypes.byref(self._make_filter_msg(mask_id)),
                ctypes.byref(self._make_filter_msg(pattern_id)),
                ctypes.byref(self._make_filter_msg(flow_id, with_pad=True)),
                ctypes.byref(fid),
            )
            ok = self._ret_ok(ret, f"PassThruStartMsgFilter[{label}]")
            if ok:
                logger.info(f"Фильтр [{label}] установлен: FilterID={fid.value}")
                return fid.value
            return 0
        except Exception as e:
            logger.debug(f"_add_flow_filter({label}): {e}")
            return 0

    def _remove_filter(self, filter_id: int) -> None:
        """Удаляет фильтр по FilterID."""
        if filter_id <= 0:
            return
        try:
            dll = self._dll_load()
            dll.PassThruStopMsgFilter(self._channel_id, ctypes.c_ulong(filter_id))
        except Exception:
            pass

    def setup_obd_filter(self) -> bool:
        """
        Устанавливает постоянный Flow-Control фильтр для стандартного диапазона
        OBD-II 0x7E8–0x7EF (PCM, TCM, …).
        Фильтры для Ford/JLR-специфичных адресов устанавливаются динамически
        в _read_dtc_from_ecu, чтобы не исчерпывать слоты (обычно 4 на канал).
        """
        if not self._is_can:
            return True  # Для K-Line фильтры не нужны

        try:
            # Широкий фильтр для стандартного OBD-II диапазона
            fid = self._add_flow_filter(
                0xFFFFFFF8, 0x000007E8, 0x000007E0, "0x7E8-0x7EF"
            )
            return fid > 0
        except Exception as e:
            logger.warning(f"J2534 setup_obd_filter exception: {e}")
            return False

    # ── Отправка / приём сообщений ────────────────────────────────────────────

    def _write_msg(self, data: bytes, timeout_ms: int = 2000) -> bool:
        """Отправляет одно сообщение в шину."""
        try:
            dll  = self._dll_load()
            msg  = PASSTHRU_MSG()
            msg.ProtocolID = self._protocol
            msg.TxFlags    = _ISO15765_PAD if self._is_can else 0
            msg.DataSize   = len(data)
            for i, b in enumerate(data):
                msg.Data[i] = b

            n   = ctypes.c_ulong(1)
            ret = dll.PassThruWriteMsgs(
                self._channel_id, ctypes.byref(msg),
                ctypes.byref(n), ctypes.c_ulong(timeout_ms)
            )
            return self._ret_ok(ret, "PassThruWriteMsgs")
        except Exception as e:
            logger.debug(f"J2534 _write_msg: {e}")
            return False

    def _read_msg(self, timeout_ms: int = 200) -> Optional[bytes]:
        """Читает одно сообщение из шины (не эхо)."""
        try:
            dll = self._dll_load()
            deadline = time.monotonic() + timeout_ms / 1000.0

            while time.monotonic() < deadline:
                msg = PASSTHRU_MSG()
                n   = ctypes.c_ulong(1)
                ret = dll.PassThruReadMsgs(
                    self._channel_id, ctypes.byref(msg),
                    ctypes.byref(n), ctypes.c_ulong(50)
                )
                if ret == J2534Err.STATUS_NOERROR and n.value > 0:
                    rxst = msg.RxStatus
                    # TX_MSG_TYPE (bit 0) — эхо отправленного сообщения, пропускаем
                    if rxst & 0x01:
                        logger.debug(f"_read_msg: пропуск TX_MSG_TYPE (RxStatus={rxst:#06x})")
                        continue
                    # ISO15765_FIRST_FRAME / TX_INDICATION (bit 2) — индикация,
                    # не сборка; пропускаем, но логируем для диагностики
                    if rxst & 0x04:
                        logger.debug(
                            f"_read_msg: пропуск RxStatus={rxst:#06x} "
                            f"(DataSize={msg.DataSize}, Data[4:]={list(msg.Data[4:min(msg.DataSize,12)])})"
                        )
                        continue
                    return bytes(msg.Data[:msg.DataSize])
                # Продолжаем ждать при: буфер пуст, таймаут, или успех-без-данных.
                # ВАЖНО: некоторые DLL (MongoosePro) возвращают STATUS_NOERROR (0x00)
                # вместо ERR_BUFFER_EMPTY (0x10) когда буфер пуст — не делаем break!
                if ret not in (J2534Err.STATUS_NOERROR,
                               J2534Err.ERR_BUFFER_EMPTY,
                               J2534Err.ERR_TIMEOUT):
                    logger.debug(f"PassThruReadMsgs вернул {ret:#x} — прерываем ожидание")
                    break
        except Exception as e:
            logger.debug(f"J2534 _read_msg: {e}")
        return None

    def _flush_rx(self, max_drain: int = 30) -> None:
        """
        Дренирует входящий буфер J2534 от накопившихся сообщений.
        Вызывать перед новым запросом, чтобы старые ответы не попали в следующее чтение.
        """
        try:
            dll = self._dll_load()
            for _ in range(max_drain):
                msg = PASSTHRU_MSG()
                n   = ctypes.c_ulong(1)
                ret = dll.PassThruReadMsgs(
                    self._channel_id, ctypes.byref(msg), ctypes.byref(n),
                    ctypes.c_ulong(0)   # timeout=0: немедленный возврат
                )
                if ret != J2534Err.STATUS_NOERROR or n.value == 0:
                    break
            else:
                logger.debug("_flush_rx: буфер сброшен (>30 сообщений)")
        except Exception:
            pass

    # ── OBD-II запросы ────────────────────────────────────────────────────────

    def _obd_request(self, mode: int, pid: Optional[int] = None,
                     timeout_ms: int = 2000) -> Optional[bytes]:
        """
        Отправляет OBD-II запрос и возвращает байты ответа (без CAN-заголовка).
        Для ISO 15765: [0x00,0x00,0x07,0xDF, mode(, pid)]
        Для K-Line:    [(len), mode(, pid)]
        pid=None для режимов без PID (Mode 03, 04).
        """
        if not self._connected:
            return None

        self._flush_rx()   # сбрасываем старые ответы из буфера

        if self._is_can:
            payload = bytes([0x00, 0x00, 0x07, 0xDF, mode] +
                            ([pid] if pid is not None else []))
        else:
            data = [mode] + ([pid] if pid is not None else [])
            payload = bytes([len(data)] + data)

        if not self._write_msg(payload, timeout_ms=timeout_ms):
            return None

        raw = self._read_msg(timeout_ms=timeout_ms)
        if not raw or len(raw) < 2:
            return None

        # Убираем 4-байтовый CAN-заголовок для ISO 15765
        return raw[4:] if self._is_can and len(raw) > 4 else raw

    def _uds_request(self, can_id: int, service: int, *args: int,
                     timeout_ms: int = 3000,
                     initial_wait_ms: int = 200) -> Optional[bytes]:
        """
        Отправляет UDS-запрос и возвращает байты ответа без CAN-заголовка.
        Обрабатывает NRC 0x78 (requestCorrectlyReceived-ResponsePending):
        ЭБУ присылает 0x7F xx 0x78 пока обрабатывает запрос — ждём финального ответа.

        initial_wait_ms: сколько мс ждать первого ответа прежде чем признать ECU
        отсутствующим. По умолчанию 200 мс (быстрое определение). Для ECU, у которых
        сессия уже подтверждена, передавайте 2000–3000 мс — ECU известен шине и просто
        может медленно формировать ответ 0x19.

        Алгоритм: опрашиваем буфер порциями по 200 мс.
        Если буфер пуст в течение initial_wait_ms — ECU не ответил (return None).
        Если получили 0x78 — ставим флаг got_pending и продолжаем ждать до deadline;
        параллельно шлём TesterPresent каждые 2 с.
        """
        if not self._connected or not self._is_can:
            return None
        self._flush_rx()
        header = [(can_id >> 24) & 0xFF, (can_id >> 16) & 0xFF,
                  (can_id >> 8)  & 0xFF,  can_id         & 0xFF]
        payload = bytes(header + [service] + list(args))
        if not self._write_msg(payload, timeout_ms=timeout_ms):
            return None

        deadline          = time.monotonic() + timeout_ms / 1000.0
        initial_deadline  = time.monotonic() + initial_wait_ms / 1000.0
        got_pending       = False
        last_tp           = time.monotonic()

        while time.monotonic() < deadline:
            # Пока ждём ответ после 0x78 — каждые 2 с шлём TesterPresent,
            # иначе ЭБУ может разорвать сессию до прихода финального ответа.
            if got_pending and (time.monotonic() - last_tp) >= 2.0:
                tp_payload = bytes(header + [0x3E, 0x00])
                self._write_msg(tp_payload, timeout_ms=200)
                last_tp = time.monotonic()

            raw = self._read_msg(timeout_ms=200)

            if not raw or len(raw) < 5:
                if got_pending:
                    continue
                # Ждём первого ответа не дольше initial_wait_ms.
                # Это позволяет медленным ECU (которые уже подтвердили сессию)
                # выдать ответ 0x19, не уходя в немедленный None.
                if time.monotonic() < initial_deadline:
                    continue
                return None

            data = raw[4:]   # снимаем 4-байтовый CAN-заголовок

            # Пропускаем ответ TesterPresent от ECU (0x7E = положительный ответ на 0x3E)
            if data and data[0] == 0x7E:
                continue

            # NRC 0x78 = ResponsePending: ЭБУ принял запрос, но ещё обрабатывает
            if len(data) >= 3 and data[0] == 0x7F and data[2] == 0x78:
                got_pending = True
                logger.debug(
                    f"UDS 0x78 ResponsePending (сервис {service:#x}) — "
                    f"продолжаем ждать ({int((deadline - time.monotonic()) * 1000)} мс осталось)"
                )
                continue

            return data

        logger.debug(f"UDS request (сервис {service:#x}): таймаут {timeout_ms} мс")
        return None

    # ── Вспомогательные методы UDS DTC ────────────────────────────────────────

    @staticmethod
    def _uds_dtc_str(b0: int, b1: int, b2: int) -> str:
        """Конвертирует 3-байтовый UDS DTC в строку типа P0574.

        Формат ISO 14229 / SAE J2012:
          b0[7:6] = система (P/C/B/U),  b0[5:4] = тип (0-3),
          b0[3:0] = первая цифра кода,  b1 = две последние цифры.
          b2 = failure-mode indicator (не часть кода, хранится отдельно).
        """
        if b0 == 0x00:
            # OBD-II в расширенном 3-байтовом формате: [0x00, b1_obd, b2_obd]
            prefix = {0: "P", 1: "C", 2: "B", 3: "U"}[(b1 >> 6) & 0x03]
            return f"{prefix}{((b1 & 0x3F) << 8 | b2):04X}"
        # Стандартная SAE/ISO 14229 кодировка из 2 байт (b0 + b1), b2 = суб-тип
        sys_map = {0b00: "P", 0b01: "C", 0b10: "B", 0b11: "U"}
        prefix     = sys_map.get((b0 >> 6) & 0x03, "P")
        type_digit = (b0 >> 4) & 0x03   # 0-3
        subsystem  = b0 & 0x0F
        return f"{prefix}{type_digit}{subsystem:X}{b1:02X}"

    @staticmethod
    def _uds_status_str(status: int) -> str:
        """
        Байт статуса DTC (ISO 14229-1 Table D.1) → читаемая строка.

        Биты:
          0x01 testFailed                   — тест провалился прямо сейчас
          0x02 testFailedThisOperationCycle — провалился в этом цикле
          0x04 pendingDTC                   — провалился один раз, ещё не подтверждён
          0x08 confirmedDTC                 — подтверждённый (несколько отказов)
          0x10 testNotCompletedSinceLastClear — тест ещё не запускался после сброса
          0x20 testFailedSinceLastClear     — был отказ после сброса, сейчас не тестируется
          0x40 testNotCompletedThisOperationCycle
          0x80 warningIndicatorRequested
        """
        if status & 0x01:   return "Active"      # прямо сейчас failing
        if status & 0x08:   return "Confirmed"   # подтверждён (≥2 цикла)
        if status & 0x04:   return "Pending"     # одиночный отказ, не подтверждён
        if status & 0x20:   return "Historical"  # был ранее, сейчас не тестируется
        return "Stored"                          # в памяти, тест не запускался с момента сброса

    # ── Данные реального времени (Mode 01) ────────────────────────────────────

    def read_realtime_data(self) -> dict:
        result = {}

        # PID → (ключ, формула)
        pids = {
            0x0C: "rpm",
            0x0D: "speed",
            0x05: "coolant",
            0x11: "throttle",
            0x0F: "intake",
            0x0A: "fuel_pressure",
            0x42: "voltage",
            0x10: "maf",
        }

        for pid, key in pids.items():
            resp = self._obd_request(0x01, pid, timeout_ms=500)
            if not resp or len(resp) < 3:
                continue
            # Проверяем positive response: 0x41 mode+0x40, затем pid
            if resp[0] != 0x41 or resp[1] != pid:
                continue
            d = resp[2:]
            try:
                if key == "rpm" and len(d) >= 2:
                    result["rpm"] = round(((d[0] << 8) | d[1]) / 4.0)
                elif key == "speed" and len(d) >= 1:
                    result["speed"] = d[0]
                elif key == "coolant" and len(d) >= 1:
                    result["coolant"] = d[0] - 40
                elif key == "throttle" and len(d) >= 1:
                    result["throttle"] = round(d[0] * 100.0 / 255.0, 1)
                elif key == "intake" and len(d) >= 1:
                    result["intake"] = d[0] - 40
                elif key == "fuel_pressure" and len(d) >= 1:
                    result["fuel_pressure"] = d[0] * 3
                elif key == "voltage" and len(d) >= 2:
                    result["voltage"] = round(((d[0] << 8) | d[1]) / 1000.0, 2)
                elif key == "maf" and len(d) >= 2:
                    result["maf"] = round(((d[0] << 8) | d[1]) / 100.0, 2)
            except Exception:
                continue

        return result

    # ── Чтение DTC: UDS 0x19 + OBD-II Mode 03 + Mode 07 ──────────────────────

    def read_dtc(self) -> List[dict]:
        """
        Читает DTC из всех доступных источников и объединяет результаты:
          1) UDS Service 0x19 — производитель-специфичные коды от всех ECU
          2) OBD-II Mode 03  — стандартные сохранённые коды (broadcast 0x7DF)
          3) OBD-II Mode 07  — ожидающие (Pending) коды
        Дубликаты по коду удаляются; источник с более подробным статусом побеждает.
        """
        seen: dict = {}   # code → entry

        def _merge(dtcs: List[dict]) -> None:
            for d in dtcs:
                code = d.get("code", "")
                if not code:
                    continue
                if code not in seen:
                    seen[code] = d
                else:
                    # Предпочитаем запись с более информативным статусом
                    existing = seen[code].get("status", "")
                    new_status = d.get("status", "")
                    if existing in ("Stored", "Active") and new_status not in ("Stored", "Active"):
                        seen[code] = d

        # 1) UDS — производитель-специфичные DTC
        uds_result = self._read_dtc_uds()
        if uds_result:
            _merge(uds_result)

        # 2) OBD Mode 03 — стандартные сохранённые DTC
        _merge(self._read_dtc_obd())

        # 3) OBD Mode 07 — ожидающие DTC
        _merge(self._read_dtc_obd_pending())

        result = list(seen.values())
        logger.info(f"DTC итого (UDS+OBD03+OBD07): {len(result)}")
        return result

    # ── Вход в UDS-сессию ──────────────────────────────────────────────────────

    def _uds_enter_session(self, session_id: int, ecu_addr: int = 0x7E0) -> bool:
        """
        UDS DiagnosticSessionControl (0x10).
        session_id: 0x01=Default, 0x02=Programming, 0x03=Extended.
        ecu_addr: физический адрес ECU (по умолчанию 0x7E0 = PCM).
        Возвращает True при положительном ответе (0x50).
        """
        data = self._uds_request(ecu_addr, 0x10, session_id, timeout_ms=2000)
        if data is None:
            logger.debug(f"UDS DSC {session_id:#x}: нет ответа")
            return False
        if len(data) >= 1 and data[0] == 0x50:
            logger.info(f"UDS сессия {session_id:#x}: OK")
            return True
        nrc = data[2] if len(data) > 2 else 0xFF
        logger.debug(f"UDS DSC {session_id:#x}: отказ, NRC={nrc:#x}, raw={list(data[:6])}")
        return False

    # ── Стандартные адреса ECU (OBD-II / UDS) ────────────────────────────────

    # Таблица ECU: (адрес_запроса, адрес_ответа, название).
    # Для диапазона 0x7E0–0x7E7 используется постоянный широкий фильтр.
    # Для остальных — динамический фильтр устанавливается перед запросом.
    _ECU_ADDRS = [
        # ── Стандартный OBD-II диапазон (0x7E0–0x7E7 → ответы 0x7E8–0x7EF) ──
        (0x7E0, 0x7E8, "PCM"),    # Powertrain Control Module
        (0x7E1, 0x7E9, "TCU"),    # Transmission Control Unit
        (0x7E2, 0x7EA, "ABS"),    # ABS/ESC (стандартный адрес)
        (0x7E3, 0x7EB, "ECU3"),
        (0x7E4, 0x7EC, "ECU4"),
        (0x7E5, 0x7ED, "ECU5"),
        (0x7E6, 0x7EE, "ECU6"),
        (0x7E7, 0x7EF, "ECU7"),
        # ── Ford / JLR специфичные адреса ────────────────────────────────────
        # Паттерн: ответ = запрос + 8 (стандарт ISO 15765-4 для модулей).
        (0x760, 0x768, "ABS"),     # ABS/ESC (Ford/JLR)
        (0x726, 0x72E, "BCMii"),   # Body Control Module intelligent interface
        (0x720, 0x728, "IPC"),     # Instrument Panel Cluster
        (0x736, 0x73E, "RCM"),     # Restraint Control Module (SRS / Airbag)
        (0x750, 0x758, "SASM"),    # Steering Angle Sensor Module
        (0x732, 0x73A, "PSCM"),    # Power Steering Control Module
        (0x740, 0x748, "AHCM"),    # Active Handling Control Module
        (0x764, 0x76C, "TCM2"),    # Transmission (альтернативный JLR-адрес)
    ]

    # ── Чтение DTC через UDS 0x19 ──────────────────────────────────────────────

    def _read_dtc_uds(self) -> Optional[List[dict]]:
        """
        UDS Service 0x19 0x02 (reportDTCByStatusMask=0xFF).
        Опрашивает все ECU из _ECU_ADDRS.
        Для не-стандартных адресов устанавливает временный flow-control фильтр.
        Возвращает объединённый список DTC или None если нет ответа ни от кого.
        """
        if not self._is_can:
            return None

        all_dtcs: List[dict] = []
        got_any_response = False

        for ecu_req, ecu_resp, ecu_name in self._ECU_ADDRS:
            dtcs = self._read_dtc_from_ecu(ecu_req, ecu_resp, ecu_name)
            if dtcs is not None:
                got_any_response = True
                all_dtcs.extend(dtcs)

        if got_any_response:
            logger.info(f"UDS DTC всего: {len(all_dtcs)} кодов от {len(self._ECU_ADDRS)} ECU")
            return all_dtcs

        logger.info("UDS 0x19: ни один ECU не ответил — переход к OBD Mode 03")
        return None

    def _read_dtc_from_ecu(self, ecu_addr: int, ecu_resp: int,
                            ecu_name: str) -> Optional[List[dict]]:
        """
        Читает DTC с одного ECU по UDS 0x19.
        Возвращает список (пустой если кодов нет), None если ECU не отвечает.

        Для ECU вне стандартного OBD-II диапазона (0x7E0–0x7E7) устанавливает
        временный flow-control фильтр, гарантируя что FC-фрейм отправляется
        с ISO15765_PAD (согласованно с нашими UDS-запросами).

        Стратегия:
          1) Пробуем войти в Extended Session 0x03.
          2) Если вошли — шлём 0x19/0x02 с ожиданием первого ответа 3 с.
          3) Если 0x19/0x02 не ответил — пробуем 0x19/0x0A (без маски статуса).
          4) Если не вошли — быстрая попытка 0x19 без сессии (500 мс).
        """
        # Для ECU вне стандартного диапазона устанавливаем временный фильтр
        is_std_range = 0x7E0 <= ecu_addr <= 0x7E7
        tmp_filter   = 0
        if not is_std_range:
            tmp_filter = self._add_flow_filter(
                0xFFFFFFFF, ecu_resp, ecu_addr,
                f"{ecu_name} {ecu_addr:#x}→{ecu_resp:#x}"
            )
            # Если фильтр не установился — пропускаем этот ECU
            if tmp_filter == 0:
                logger.debug(f"{ecu_name} ({ecu_addr:#x}): не удалось установить фильтр, пропуск")
                return None

        try:
            # Шаг 1: Extended Diagnostic Session
            entered = self._uds_enter_session(0x03, ecu_addr=ecu_addr)

            if entered:
                dtc_timeout     = 15000
                initial_wait_ms = 3000
            else:
                dtc_timeout     = 2000
                initial_wait_ms = 500

            data = self._uds_request(ecu_addr, 0x19, 0x02, 0xFF,
                                     timeout_ms=dtc_timeout,
                                     initial_wait_ms=initial_wait_ms)

            # Fallback: 0x19/0x0A (reportAllSupportedDTCs — без маски статуса)
            if data is None and entered:
                logger.debug(f"{ecu_name} ({ecu_addr:#x}): пробуем 0x19/0x0A как fallback")
                data = self._uds_request(ecu_addr, 0x19, 0x0A,
                                         timeout_ms=dtc_timeout,
                                         initial_wait_ms=initial_wait_ms)

            if data is None:
                logger.debug(f"{ecu_name} ({ecu_addr:#x}): нет ответа на 0x19")
                return None

            # 0x19/0x02 → ответ 0x59/0x02; 0x19/0x0A → ответ 0x59/0x0A
            if len(data) >= 3 and data[0] == 0x59 and data[1] in (0x02, 0x0A):
                dtcs = self._parse_uds_dtc_response(data, source=ecu_name)
                logger.info(f"{ecu_name} ({ecu_addr:#x}): {len(dtcs)} DTC")
                return dtcs

            if data[0] == 0x7F:
                nrc = data[2] if len(data) > 2 else 0xFF
                if nrc in (0x11, 0x12):
                    logger.debug(f"{ecu_name} ({ecu_addr:#x}): UDS 0x19 не поддерживается (NRC={nrc:#x})")
                    return None
                logger.debug(f"{ecu_name} ({ecu_addr:#x}): NRC={nrc:#x}")
                return None

            logger.debug(f"{ecu_name} ({ecu_addr:#x}): неожиданный ответ {list(data[:4])}")
            return None

        finally:
            self._remove_filter(tmp_filter)

    def _parse_uds_dtc_response(self, data: bytes,
                                source: str = "ECU") -> List[dict]:
        """Парсит ответ UDS 0x59 0x02 → список DTC."""
        dtc_list: List[dict] = []
        i = 3   # пропускаем [0x59, 0x02, statusAvailabilityMask]
        while i + 3 < len(data):
            b0, b1, b2, status = data[i], data[i+1], data[i+2], data[i+3]
            i += 4
            if b0 == 0 and b1 == 0 and b2 == 0:
                continue
            # Для b0 != 0: b2 — failure-mode indicator (как у ForScan: P0674:00-6C)
            code       = self._uds_dtc_str(b0, b1, b2)
            sub_type   = b2 if b0 != 0x00 else None
            status_str = self._uds_status_str(status)
            code_full  = f"{code}:{sub_type:02X}" if sub_type is not None else code
            dtc_list.append({
                "code":        code_full,
                "status":      status_str,
                "description": f"DTC {code_full} [{source}] (статус: {status:#04x})",
                "timestamp":   "-",
            })
        return dtc_list

    # ── Чтение DTC через OBD-II Mode 03 (fallback) ────────────────────────────

    def _read_dtc_obd(self) -> List[dict]:
        """OBD-II Mode 03 — запасной вариант для старых ECU."""
        resp = self._obd_request(0x03, timeout_ms=3000)
        dtc_list = []
        if not resp or resp[0] != 0x43:
            logger.debug(f"OBD Mode03: нет ответа или resp[0]!={resp[0] if resp else 'None'}")
            return dtc_list
        i = 1
        while i + 1 < len(resp):
            b1, b2 = resp[i], resp[i + 1]
            if b1 == 0 and b2 == 0:
                break
            prefix = {0: "P", 1: "C", 2: "B", 3: "U"}[(b1 >> 6) & 0x03]
            code   = f"{prefix}{((b1 & 0x3F) << 8 | b2):04X}"
            dtc_list.append({
                "code":        code,
                "status":      "Active",
                "description": f"DTC ({code})",
                "timestamp":   "-",
            })
            i += 2
        logger.info(f"OBD Mode03 DTC: найдено {len(dtc_list)}")
        return dtc_list

    def _read_dtc_obd_pending(self) -> List[dict]:
        """OBD-II Mode 07 — ожидающие (Pending) DTC."""
        resp = self._obd_request(0x07, timeout_ms=3000)
        dtc_list = []
        if not resp or resp[0] != 0x47:
            logger.debug(f"OBD Mode07: нет ответа или resp[0]!={resp[0] if resp else 'None'}")
            return dtc_list
        i = 1
        while i + 1 < len(resp):
            b1, b2 = resp[i], resp[i + 1]
            if b1 == 0 and b2 == 0:
                break
            prefix = {0: "P", 1: "C", 2: "B", 3: "U"}[(b1 >> 6) & 0x03]
            code   = f"{prefix}{((b1 & 0x3F) << 8 | b2):04X}"
            dtc_list.append({
                "code":        code,
                "status":      "Pending",
                "description": f"DTC ({code})",
                "timestamp":   "-",
            })
            i += 2
        logger.info(f"OBD Mode07 Pending DTC: найдено {len(dtc_list)}")
        return dtc_list

    # ── Стирание DTC: UDS 0x14 + OBD-II Mode 04 (fallback) ───────────────────

    def clear_dtc(self) -> bool:
        """UDS Service 0x14 (ClearDiagnosticInformation), fallback — OBD-II Mode 04."""
        if self._is_can:
            # UDS: 0x14 + 0xFFFFFF (все группы DTC)
            data = self._uds_request(0x7E0, 0x14, 0xFF, 0xFF, 0xFF, timeout_ms=5000)
            if data and data[0] == 0x54:
                logger.info("UDS clear DTC: OK")
                return True
            logger.debug(f"UDS 0x14 не сработал: {data}")

        # Fallback: OBD-II Mode 04
        resp = self._obd_request(0x04, timeout_ms=5000)
        ok = bool(resp and resp[0] == 0x44)
        logger.info(f"OBD clear DTC: {'OK' if ok else 'FAIL'}")
        return ok

    # ── VIN (Mode 09 PID 02) ──────────────────────────────────────────────────

    def read_vin(self) -> str:
        resp = self._obd_request(0x09, 0x02, timeout_ms=3000)
        if resp and len(resp) >= 4 and resp[0] == 0x49 and resp[1] == 0x02:
            vin_raw = resp[3:]
            try:
                return bytes(b for b in vin_raw if 0x20 <= b < 0x7F).decode("ascii")
            except Exception:
                pass
        return ""

    # ── Информация об ЭБУ (UDS 0x22 + OBD Mode 09) ───────────────────────────

    def read_ecu_info(self) -> dict:
        """
        Читает идентификационные данные ЭБУ.

        Порядок чтения:
          1) OBD Mode 09 PID 0x02 → VIN (широковещательный запрос 0x7DF)
          2) OBD Mode 09 PID 0x04 → Calibration ID (CALID)
          3) OBD Mode 09 PID 0x0A → ECU Name
          4) Вход в Extended Session 0x03 на PCM (0x7E0) — нужен для UDS 0x22
          5) UDS 0x22 0xF190      → VIN (если Mode 09 не ответил)
          6) UDS 0x22 0xF18C      → ECU Serial Number
          7) UDS 0x22 0xF197      → System Name / Engine Type
          8) UDS 0x22 0xF187      → Spare Part Number
          9) UDS 0x22 0xF188      → Application Software Identification

        Возвращает dict с человекочитаемыми ключами (пустой, если ничего не прочиталось).
        """
        info: dict = {}

        def _ascii(raw: bytes) -> str:
            return bytes(b for b in raw if 0x20 <= b < 0x7F).decode("ascii", errors="ignore").strip()

        def _rdbi(did_hi: int, did_lo: int, label: str) -> None:
            """Читает один RDBI DID и кладёт в info если ответ корректен."""
            r = self._uds_request(
                0x7E0, 0x22, did_hi, did_lo,
                timeout_ms=3000, initial_wait_ms=500,
            )
            if r and len(r) > 3 and r[0] == 0x62:
                val = _ascii(r[3:])
                if val:
                    info[label] = val
                    logger.debug(f"read_ecu_info: {label} = {val!r}")

        # ── 1) OBD Mode 09 PID 0x02 → VIN ────────────────────────────────────
        r = self._obd_request(0x09, 0x02, timeout_ms=3000)
        if r and len(r) >= 4 and r[0] == 0x49 and r[1] == 0x02:
            vin = _ascii(r[3:])
            if vin:
                info["VIN"] = vin

        # ── 2) OBD Mode 09 PID 0x04 → Calibration ID ─────────────────────────
        r = self._obd_request(0x09, 0x04, timeout_ms=3000)
        if r and len(r) >= 3 and r[0] == 0x49 and r[1] == 0x04:
            calid = _ascii(r[3:] if len(r) > 3 else r[2:])
            if calid:
                info["Калибровка (CALID)"] = calid

        # ── 3) OBD Mode 09 PID 0x0A → ECU Name ───────────────────────────────
        r = self._obd_request(0x09, 0x0A, timeout_ms=3000)
        if r and len(r) >= 3 and r[0] == 0x49 and r[1] == 0x0A:
            name = _ascii(r[2:])
            if name:
                info["Имя ЭБУ"] = name

        if not self._is_can:
            return info

        # ── 4) Входим в Extended Session — нужна для UDS 0x22 на JLR ─────────
        self._uds_enter_session(0x03, ecu_addr=0x7E0)

        # ── 5) UDS 0x22 0xF190 → VIN (если OBD не дал) ───────────────────────
        if "VIN" not in info:
            r = self._uds_request(0x7E0, 0x22, 0xF1, 0x90,
                                  timeout_ms=3000, initial_wait_ms=500)
            if r and len(r) > 3 and r[0] == 0x62:
                vin = _ascii(r[3:])
                if vin:
                    info["VIN"] = vin

        # ── 6-9) Идентификационные DID ────────────────────────────────────────
        _rdbi(0xF1, 0x8C, "Серийный номер ЭБУ")      # 0xF18C ECU Serial Number
        _rdbi(0xF1, 0x97, "Тип двигателя")            # 0xF197 System Name
        _rdbi(0xF1, 0x87, "Номер блока")              # 0xF187 Spare Part Number
        _rdbi(0xF1, 0x88, "Версия ПО")                # 0xF188 Application SW ID

        if info:
            logger.info(f"read_ecu_info: получено {len(info)} полей: {list(info.keys())}")
        else:
            logger.debug("read_ecu_info: ни один запрос не дал ответа от PCM")

        return info

    # ── Пинг / качество CAN-соединения ───────────────────────────────────────

    def ping(self) -> Optional[float]:
        """
        Отправляет UDS TesterPresent (0x3E 0x00) на PCM и измеряет RTT в мс.
        Возвращает время ответа или None если ECU не отвечает.
        Используется для расчёта качества соединения.
        """
        if not self._connected or not self._is_can:
            return None
        try:
            t0 = time.monotonic()
            # 0x3E = TesterPresent, 0x00 = не подавлять ответ
            data = self._uds_request(0x7E0, 0x3E, 0x00, timeout_ms=1000)
            rtt = (time.monotonic() - t0) * 1000
            if data is not None and len(data) >= 1 and data[0] == 0x7E:
                return rtt
        except Exception:
            pass
        return None

    # ── Версия прошивки / DLL ─────────────────────────────────────────────────

    def read_version(self) -> dict:
        try:
            dll  = self._dll_load()
            fw   = ctypes.create_string_buffer(80)
            dllv = ctypes.create_string_buffer(80)
            apiv = ctypes.create_string_buffer(80)
            ret  = dll.PassThruReadVersion(self._device_id, fw, dllv, apiv)
            if self._ret_ok(ret, "PassThruReadVersion"):
                return {
                    "firmware": fw.value.decode("ascii", errors="ignore"),
                    "dll":      dllv.value.decode("ascii", errors="ignore"),
                    "api":      apiv.value.decode("ascii", errors="ignore"),
                }
        except Exception as e:
            logger.debug(f"J2534 read_version: {e}")
        return {}

    # ── Статус ────────────────────────────────────────────────────────────────

    def is_connected(self) -> bool:
        return self._connected

    def get_last_error(self) -> str:
        try:
            dll = self._dll_load()
            buf = ctypes.create_string_buffer(80)
            dll.PassThruGetLastError(buf)
            return buf.value.decode("ascii", errors="ignore")
        except Exception:
            return ""


# ─── Вспомогательные функции для разрядности DLL ──────────────────────────────

def _get_dll_arch(dll_path: str) -> int:
    """Читает PE-заголовок DLL и возвращает 32 или 64."""
    try:
        with open(dll_path, "rb") as f:
            f.seek(0x3C)
            pe_offset = int.from_bytes(f.read(4), "little")
            f.seek(pe_offset + 4)
            machine = int.from_bytes(f.read(2), "little")
        # 0x8664 = AMD64 (64-bit), 0x014C = i386 (32-bit)
        return 64 if machine == 0x8664 else 32
    except Exception:
        return 32  # при ошибке считаем 32-bit


def _find_python32() -> Optional[str]:
    """Ищет 32-bit Python через py.exe launcher или известные пути."""
    import subprocess as _sp

    # Метод 1: Python Launcher (py.exe), устанавливается с python.org
    for flag in ["-3-32", "-3.13-32", "-3.12-32", "-3.11-32", "-3.10-32", "-3.9-32"]:
        try:
            r = _sp.run(
                ["py", flag, "-c", "import sys; print(sys.executable)"],
                capture_output=True, text=True, timeout=5,
            )
            if r.returncode == 0 and r.stdout.strip():
                return r.stdout.strip()
        except Exception:
            continue

    # Метод 2: известные директории установки
    for ver in ["313-32", "312-32", "311-32", "310-32", "39-32"]:
        path = r"C:\Python" + ver + r"\python.exe"
        if _os.path.isfile(path):
            return path

    return None


# ─── Subprocess-bridge для 32-bit DLL из 64-bit Python ────────────────────────

class J2534PassThruBridge:
    """
    Замена J2534PassThru для случая когда DLL 32-bit, а Python 64-bit.
    Запускает j2534_bridge32.py под 32-bit Python как subprocess,
    общается через JSON stdin/stdout. Публичный API совместим с J2534PassThru.
    """

    def __init__(self, dll_path: str, python32_exe: str):
        self._dll_path  = dll_path
        self._python32  = python32_exe
        self._proc      = None   # subprocess.Popen | None
        self._connected = False

    # ── запуск / остановка subprocess ─────────────────────────────────────────

    def _start(self) -> bool:
        import subprocess as _sp
        bridge = _os.path.join(_os.path.dirname(__file__), "j2534_bridge32.py")
        if not _os.path.isfile(bridge):
            logger.error(f"J2534 bridge не найден: {bridge}")
            return False
        try:
            self._proc = _sp.Popen(
                [self._python32, bridge, self._dll_path],
                stdin=_sp.PIPE, stdout=_sp.PIPE,
                stderr=None,   # наследуем stderr основного процесса → логи видны в файле
                text=True, bufsize=1,
            )
            ready_line = self._proc.stdout.readline()
            ready = _json.loads(ready_line)
            if not ready.get("ok"):
                logger.error(f"J2534 bridge ошибка запуска: {ready.get('error')}")
                self._proc.kill()
                self._proc = None
                return False
            logger.info("J2534 bridge (32-bit) запущен")
            return True
        except Exception as e:
            logger.error(f"J2534 bridge запуск: {e}")
            return False

    def _call(self, method: str, *args, **kwargs):
        """Отправляет команду в bridge и возвращает result."""
        if self._proc is None or self._proc.poll() is not None:
            raise RuntimeError("J2534 bridge не запущен")
        cmd = _json.dumps({"method": method, "args": list(args), "kwargs": kwargs})
        self._proc.stdin.write(cmd + "\n")
        self._proc.stdin.flush()
        resp = _json.loads(self._proc.stdout.readline())
        if not resp.get("ok"):
            raise RuntimeError(resp.get("error", "Bridge error"))
        return resp.get("result")

    def _stop(self):
        if self._proc is not None:
            try:
                self._proc.stdin.write(_json.dumps({"method": "exit"}) + "\n")
                self._proc.stdin.flush()
                self._proc.wait(timeout=3)
            except Exception:
                self._proc.kill()
            self._proc = None

    # ── публичный API (совместим с J2534PassThru) ─────────────────────────────

    def open(self) -> bool:
        if not self._start():
            return False
        try:
            return bool(self._call("open"))
        except Exception as e:
            logger.error(f"J2534 bridge open: {e}")
            return False

    def close(self):
        try:
            if self._proc is not None:
                self._call("close")
        except Exception:
            pass
        self._stop()

    def connect(self, protocol: int = J2534Proto.ISO15765,
                flags: int = 0, baud_rate: int = 500000) -> bool:
        try:
            return bool(self._call("connect",
                                   protocol=protocol, flags=flags, baud_rate=baud_rate))
        except Exception as e:
            logger.error(f"J2534 bridge connect: {e}")
            return False

    def auto_connect(self) -> bool:
        try:
            return bool(self._call("auto_connect"))
        except Exception as e:
            logger.error(f"J2534 bridge auto_connect: {e}")
            return False

    def disconnect_channel(self):
        try:
            self._call("disconnect_channel")
        except Exception:
            pass
        self._connected = False

    def disconnect(self):
        try:
            self._call("disconnect")
        except Exception:
            pass
        self._connected = False
        self._stop()

    def setup_obd_filter(self) -> bool:
        try:
            return bool(self._call("setup_obd_filter"))
        except Exception:
            return False

    def read_realtime_data(self) -> dict:
        try:
            return self._call("read_realtime_data") or {}
        except Exception:
            return {}

    def read_dtc(self) -> List[dict]:
        try:
            return self._call("read_dtc") or []
        except Exception:
            return []

    def clear_dtc(self) -> bool:
        try:
            return bool(self._call("clear_dtc"))
        except Exception:
            return False

    def read_vin(self) -> str:
        try:
            return self._call("read_vin") or ""
        except Exception:
            return ""

    def read_version(self) -> dict:
        try:
            return self._call("read_version") or {}
        except Exception:
            return {}

    def read_ecu_info(self) -> dict:
        try:
            return self._call("read_ecu_info") or {}
        except Exception:
            return {}

    def is_connected(self) -> bool:
        try:
            return bool(self._call("is_connected"))
        except Exception:
            return False

    def get_last_error(self) -> str:
        return ""


# ─── Фабрика ──────────────────────────────────────────────────────────────────

def create_j2534_adapter(dll_path: str):
    """
    Возвращает J2534PassThru (прямой ctypes) или J2534PassThruBridge (subprocess),
    в зависимости от разрядности DLL и текущего Python.
    """
    import struct as _struct
    python_bits = _struct.calcsize("P") * 8  # 32 или 64

    if python_bits == 64:
        dll_arch = _get_dll_arch(dll_path)
        if dll_arch == 32:
            logger.info(
                f"DLL 32-bit, Python 64-bit — запускаю subprocess bridge: {dll_path}"
            )
            py32 = _find_python32()
            if py32 is None:
                raise RuntimeError(
                    "Обнаружена 32-bit J2534 DLL, но 32-bit Python не найден.\n"
                    "Установите 32-bit Python (python.org → «Windows installer (32-bit)»)"
                    " или запустите приложение через 32-bit Python."
                )
            logger.info(f"32-bit Python: {py32}")
            return J2534PassThruBridge(dll_path, py32)

    return J2534PassThru(dll_path)
