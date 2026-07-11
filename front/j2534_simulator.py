"""
j2534_simulator.py — J2534PassThru с виртуальным ЭБУ вместо реального адаптера.

Переопределяет только границу, где J2534PassThru обращается к железу через
ctypes (_write_msg/_read_msg и открытие/подключение канала) — вся остальная
логика (_uds_request, read_memory, write_memory, request_seed/send_key,
enter_session и т.д.) выполняется БЕЗ ИЗМЕНЕНИЙ, как с реальным адаптером.
Это значит: то, что проверено против симулятора, использует тот же код,
что пойдёт в реальный J2534-адаптер — меняется только транспорт.

Используется для тестирования без физического J2534-адаптера/автомобиля
(см. виртуальный ЭБУ в virtual_ecu.py).
"""

import os
import sys
from typing import Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from j2534_passthru import J2534PassThru, J2534Proto  # noqa: E402
from virtual_ecu import VirtualECU  # noqa: E402


class J2534PassThruSimulated(J2534PassThru):
    """
    Тестовая замена J2534PassThru: вместо ctypes/DLL отвечает VirtualECU.

    ecu_map: {request_id: VirtualECU} — можно смоделировать несколько ЭБУ на
    одной шине (по умолчанию один ЭБУ на стандартном адресе 0x7E0/PCM).
    """

    def __init__(self, ecu_map: Optional[dict] = None):
        super().__init__(dll_path="<simulated>")
        self._ecu_map = ecu_map or {0x7E0: VirtualECU(ecu_name="SIM-PCM")}
        self._rx_queue: list = []

    def ecu(self, request_id: int = 0x7E0) -> VirtualECU:
        """Доступ к виртуальному ЭБУ из тестового кода (проверка written_log и т.п.)."""
        return self._ecu_map[request_id]

    # ── переопределяем границу с "железом" ────────────────────────────────

    def _dll_load(self):
        return None  # DLL не используется

    def open(self) -> bool:
        self._device_id.value = 1
        return True

    def close(self):
        pass

    def connect(self, protocol: int = J2534Proto.ISO15765,
                flags: int = 0, baud_rate: int = 500000) -> bool:
        self._protocol = protocol
        self._is_can = True
        self._connected = True
        self._channel_id.value = 1
        return True

    def auto_connect(self) -> bool:
        return self.connect()

    def disconnect_channel(self):
        self._connected = False

    def disconnect(self):
        self.disconnect_channel()

    def setup_obd_filter(self) -> bool:
        return True

    def _flush_rx(self, max_drain: int = 30) -> None:
        self._rx_queue.clear()

    def _write_msg(self, data: bytes, timeout_ms: int = 2000) -> bool:
        if len(data) < 5:
            return False
        can_id = int.from_bytes(data[:4], "big")
        service = data[4]
        payload = data[5:]

        ecu = self._ecu_map.get(can_id)
        if ecu is None:
            return True  # запрос "уходит в пустоту" — как на реальной шине без ответа

        resp_id = can_id + 8  # стандартный сдвиг ISO 15765-4 между запросом и ответом
        response = ecu.handle_uds_service(service, payload)
        self._rx_queue.append(
            resp_id.to_bytes(4, "big") + bytes(response)
        )
        return True

    def _read_msg(self, timeout_ms: int = 200) -> Optional[bytes]:
        if self._rx_queue:
            return self._rx_queue.pop(0)
        return None
