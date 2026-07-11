"""
virtual_ecu.py — общая модель "виртуального ЭБУ" для тестирования UDS-стека
(чтение/запись памяти, Security Access) без реального адаптера/автомобиля.

Используется:
  - elm_simulator.py       (TCP ELM327-симулятор, диспетчер по hex-командам)
  - front/j2534_simulator.py (подмена J2534PassThru._write_msg/_read_msg)

Реализует минимальный, но настоящий цикл UDS-сервисов: DiagnosticSessionControl
(0x10), SecurityAccess (0x27 — ТЕСТОВЫЙ seed/key, НЕ алгоритм производителя,
см. front/security_access.py), ReadMemoryByAddress (0x23),
RequestDownload/TransferData/RequestTransferExit (0x34/0x36/0x37),
TesterPresent (0x3E).
"""

NEG_RESPONSE = 0x7F

# Negative Response Codes (ISO 14229-1)
NRC_SERVICE_NOT_SUPPORTED = 0x11
NRC_CONDITIONS_NOT_CORRECT = 0x22
NRC_INVALID_KEY = 0x35
NRC_SECURITY_ACCESS_DENIED = 0x33
NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70
NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73


class NegativeResponse(Exception):
    """Внутренний сигнал для handle_uds_service: сформировать 0x7F-ответ с этим NRC."""

    def __init__(self, nrc: int):
        self.nrc = nrc
        super().__init__(f"NRC {nrc:#04x}")


class VirtualECU:
    """
    Фейковый ЭБУ для тестов: детерминированная "прошивка" в памяти, тестовый
    seed/key (см. compute_test_key — НЕ реальный алгоритм) и учёт записанных
    через RequestDownload/TransferData байт (written_log) для проверки в тестах.
    """

    MEMORY_SIZE = 64 * 1024
    TEST_SEED = bytes([0x12, 0x34])
    _XOR_KEY = 0xA5  # тестовый "алгоритм" — только для проверки протокола в симуляторе

    def __init__(self, ecu_name: str = "SIM-ECU"):
        self.ecu_name = ecu_name
        self.session = 0x01  # 0x01 default, 0x02 programming, 0x03 extended
        self.unlocked = False
        self._pending_seed_level = None

        # Детерминированная фейковая "прошивка": byte(addr) = addr % 256.
        self.memory = bytearray(i % 256 for i in range(self.MEMORY_SIZE))
        self.written_log: dict[int, bytes] = {}  # address -> записанные байты

        self._download_active = False
        self._download_address = 0
        self._download_block_size = 0x100
        self._expected_block_seq = 1
        self._download_buffer = bytearray()

    @classmethod
    def compute_test_key(cls, seed: bytes) -> bytes:
        """Тестовый seed→key (XOR), существует ТОЛЬКО для проверки протокола
        в симуляторе — реального алгоритма производителя это не заменяет."""
        return bytes(b ^ cls._XOR_KEY for b in seed)

    def handle_uds_service(self, service: int, data) -> list:
        """
        data — список/bytes байт ПОСЛЕ service_id. Возвращает список байт ответа
        (первый байт — код положительного/отрицательного ответа).
        """
        handler = {
            0x10: self._session_control,
            0x27: self._security_access,
            0x23: self._read_memory,
            0x34: self._request_download,
            0x36: self._transfer_data,
            0x37: self._request_transfer_exit,
            0x3E: self._tester_present,
        }.get(service)

        if handler is None:
            return [NEG_RESPONSE, service, NRC_SERVICE_NOT_SUPPORTED]

        try:
            return handler(list(data))
        except NegativeResponse as e:
            return [NEG_RESPONSE, service, e.nrc]

    # ── UDS-сервисы ─────────────────────────────────────────────────────────

    def _session_control(self, data):
        if not data:
            raise NegativeResponse(NRC_CONDITIONS_NOT_CORRECT)
        self.session = data[0]
        if self.session == 0x01:
            self.unlocked = False
        return [0x50, self.session, 0x00, 0x19, 0x01, 0xF4]

    def _security_access(self, data):
        if not data:
            raise NegativeResponse(NRC_CONDITIONS_NOT_CORRECT)
        level = data[0]

        if level % 2 == 1:
            # Нечётный уровень — запрос seed.
            if self.unlocked:
                return [0x67, level]  # пустой seed: уже разблокирован
            self._pending_seed_level = level
            return [0x67, level] + list(self.TEST_SEED)

        # Чётный уровень — отправка ключа.
        key = bytes(data[1:])
        expected = self.compute_test_key(self.TEST_SEED)
        if self._pending_seed_level is None or key != expected:
            self._pending_seed_level = None
            raise NegativeResponse(NRC_INVALID_KEY)

        self.unlocked = True
        self._pending_seed_level = None
        return [0x67, level]

    def _require_unlocked(self):
        if not self.unlocked:
            raise NegativeResponse(NRC_SECURITY_ACCESS_DENIED)

    def _read_memory(self, data):
        if not data:
            raise NegativeResponse(NRC_CONDITIONS_NOT_CORRECT)
        address, size, _ = self._parse_addr_len(data, start=1, addr_len_fmt=data[0])
        if address + size > len(self.memory):
            raise NegativeResponse(NRC_CONDITIONS_NOT_CORRECT)
        return [0x63] + list(self.memory[address:address + size])

    def _request_download(self, data):
        self._require_unlocked()
        if self.session != 0x02:
            raise NegativeResponse(NRC_CONDITIONS_NOT_CORRECT)
        if len(data) < 2:
            raise NegativeResponse(NRC_CONDITIONS_NOT_CORRECT)

        address, size, _ = self._parse_addr_len(data, start=2, addr_len_fmt=data[1])

        self._download_active = True
        self._download_address = address
        self._expected_block_seq = 1
        self._download_buffer = bytearray()

        block = self._download_block_size
        return [0x74, 0x20, (block >> 8) & 0xFF, block & 0xFF]

    def _transfer_data(self, data):
        self._require_unlocked()
        if not self._download_active:
            raise NegativeResponse(NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED)
        if not data:
            raise NegativeResponse(NRC_CONDITIONS_NOT_CORRECT)

        block_seq = data[0]
        if block_seq != self._expected_block_seq:
            raise NegativeResponse(NRC_WRONG_BLOCK_SEQUENCE_COUNTER)

        self._download_buffer += bytes(data[1:])
        self._expected_block_seq = (self._expected_block_seq % 0xFF) + 1
        return [0x76, block_seq]

    def _request_transfer_exit(self, data):
        self._require_unlocked()
        if not self._download_active:
            raise NegativeResponse(NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED)

        end = self._download_address + len(self._download_buffer)
        if end <= len(self.memory):
            self.memory[self._download_address:end] = self._download_buffer
        self.written_log[self._download_address] = bytes(self._download_buffer)

        self._download_active = False
        return [0x77]

    def _tester_present(self, data):
        return [0x7E, 0x00]

    # ── вспомогательное ─────────────────────────────────────────────────────

    @staticmethod
    def _parse_addr_len(data, start: int, addr_len_fmt: int):
        """Разбирает addressAndLengthFormatIdentifier + memoryAddress + memorySize."""
        addr_bytes = addr_len_fmt & 0x0F
        size_bytes = (addr_len_fmt >> 4) & 0x0F
        idx = start
        address = int.from_bytes(bytes(data[idx:idx + addr_bytes]), "big")
        idx += addr_bytes
        size = int.from_bytes(bytes(data[idx:idx + size_bytes]), "big")
        idx += size_bytes
        return address, size, idx
