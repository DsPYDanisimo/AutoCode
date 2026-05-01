# front/api_client.py
import requests
import threading
import time
import logging
import os
from datetime import datetime
from urllib.parse import quote
from PyQt5.QtCore import QObject, pyqtSignal, QMetaObject, Qt, Q_ARG
import traceback

# Настройка логирования
LOG_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "logs")
os.makedirs(LOG_DIR, exist_ok=True)

log_filename = os.path.join(LOG_DIR, f"api_client_{datetime.now().strftime('%Y%m%d')}.log")
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler(log_filename, encoding='utf-8'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)


class ECUAPIClient(QObject):
    connection_status_changed = pyqtSignal(dict)
    ports_update = pyqtSignal(list)
    ecu_data_received = pyqtSignal(dict)
    connection_quality_changed = pyqtSignal(int)
    ecu_data_cleared = pyqtSignal()

    def __init__(self, host="localhost", port=8080):
        super().__init__()
        self.base_url = f"http://{host}:{port}/api"
        self.monitoring_thread = None
        self.quality_thread = None
        self.is_monitoring = False
        self._is_connected = False
        self.current_connection = {"connected": False, "protocol": "None", "port": "None"}
        self.connection_start_time = None
        self.signal_quality = 0
        self.dtc_cache = {}
        self.lock = threading.RLock()
        self.reconnect_attempts = 0
        self.max_reconnect_attempts = 5
        self.logger_queue = []  # Очередь для сообщений лога
        self.logger_timer = None
        self._j2534 = None     # Активное J2534 соединение (J2534PassThru | None)
        logger.info(f"API Client инициализирован: {self.base_url}")

    @property
    def is_connected(self):
        with self.lock:
            return self._is_connected

    @is_connected.setter
    def is_connected(self, value):
        with self.lock:
            self._is_connected = value

    def log_message(self, message):
        """Безопасное добавление сообщения в лог через сигнал"""
        # Отправляем сигнал для добавления в лог в главном потоке
        self.ecu_data_received.emit({"log": message})

    def start_monitoring(self, interval=2):
        self.is_monitoring = True
        self.monitoring_thread = threading.Thread(target=self._monitor_connection,
                                                  args=(interval,),
                                                  daemon=True)
        self.monitoring_thread.start()

        self.quality_thread = threading.Thread(target=self._monitor_quality,
                                               args=(5,),
                                               daemon=True)
        self.quality_thread.start()
        logger.info("Мониторинг запущен")

    def stop_monitoring(self):
        self.is_monitoring = False
        if self.monitoring_thread:
            self.monitoring_thread.join(timeout=1)
        if self.quality_thread:
            self.quality_thread.join(timeout=1)
        logger.info("Мониторинг остановлен")

    def _monitor_connection(self, interval):
        consecutive_failures = 0
        max_failures = 3
        stable_connection_counter = 0
        disconnect_counter = 0       # сколько раз подряд сервер говорит "not connected"
        max_disconnect_confirms = 2  # нужно N подтверждений перед сбросом _is_connected

        while self.is_monitoring:
            try:
                status = self.get_connection_status()
                consecutive_failures = 0  # сброс при успешном HTTP-ответе

                # Добавляем небольшую задержку для стабильности
                time.sleep(0.1)

                with self.lock:
                    old_connected = self._is_connected
                    new_connected = status.get("connected", False)

                    if new_connected:
                        stable_connection_counter += 1
                        disconnect_counter = 0
                        # Требуем несколько успешных проверок подряд для подтверждения подключения
                        if stable_connection_counter >= 2:
                            self._is_connected = True
                            self.reconnect_attempts = 0
                            consecutive_failures = 0

                            if not old_connected:
                                self.connection_start_time = datetime.now()
                                status["connected_since"] = self.connection_start_time.isoformat()
                                logger.info(f"Подключение установлено: {status.get('protocol')} на {status.get('port')}")
                    else:
                        stable_connection_counter = 0
                        disconnect_counter += 1
                        # Сбрасываем только после нескольких подтверждений разрыва,
                        # чтобы не перетирать _is_connected=True, выставленный connect_to_port
                        if disconnect_counter >= max_disconnect_confirms:
                            self._is_connected = False
                            self.connection_start_time = None
                            if old_connected:
                                logger.info("Подключение разорвано")

                    # Обновляем время подключения
                    if self._is_connected and self.connection_start_time:
                        elapsed = datetime.now() - self.connection_start_time
                        status["connected_for"] = str(elapsed).split('.')[0]

                    if status != self.current_connection:
                        self.current_connection = status
                        self.connection_status_changed.emit(status)

                time.sleep(interval)

            except Exception as e:
                logger.error(f"Ошибка в мониторинге: {e}")
                consecutive_failures += 1
                stable_connection_counter = 0

                if consecutive_failures >= max_failures:
                    with self.lock:
                        if self._is_connected:
                            self._is_connected = False
                            self.connection_start_time = None
                            status = {"connected": False, "protocol": "None", "port": "None",
                                     "error": str(e)}
                            self.current_connection = status
                            self.connection_status_changed.emit(status)
                            logger.info("Подключение разорвано (ошибка)")

                time.sleep(interval)

    def _monitor_quality(self, interval):
        while self.is_monitoring:
            try:
                with self.lock:
                    is_connected = self._is_connected
                    j2534        = self._j2534

                if is_connected:
                    if j2534 is not None:
                        # ── J2534: меряем качество по RTT TesterPresent на CAN-шине
                        quality = self._measure_j2534_quality(j2534)
                    else:
                        # ── HTTP-бэкенд: меряем по задержке REST-ответа
                        quality = self._measure_http_quality()

                    with self.lock:
                        self.signal_quality = quality
                    self.connection_quality_changed.emit(quality)
                else:
                    with self.lock:
                        if self.signal_quality != 0:
                            self.signal_quality = 0
                            self.connection_quality_changed.emit(0)

            except Exception as e:
                logger.debug(f"Ошибка измерения качества: {e}")

            time.sleep(interval)

    def _measure_j2534_quality(self, j2534) -> int:
        """Качество J2534: RTT TesterPresent → процент (0–100)."""
        try:
            rtt = j2534.ping()
            if rtt is None:
                # Нет ответа — но соединение ещё числится активным; небольшой штраф
                with self.lock:
                    return max(30, self.signal_quality - 5)
            # RTT → качество
            if rtt < 20:    return 100
            if rtt < 50:    return 90
            if rtt < 100:   return 80
            if rtt < 200:   return 70
            if rtt < 500:   return 55
            if rtt < 1000:  return 40
            return 30
        except Exception as e:
            logger.debug(f"J2534 ping ошибка: {e}")
            with self.lock:
                return max(0, self.signal_quality - 10)

    def _measure_http_quality(self) -> int:
        """Качество HTTP-бэкенда: задержка REST /connection/status → процент."""
        try:
            start = time.time()
            response = requests.get(f"{self.base_url}/connection/status",
                                    timeout=2,
                                    headers={"Connection": "keep-alive"})
            rtt_ms = (time.time() - start) * 1000
            if response.status_code == 200:
                q = response.json().get("signal_quality", 0)
                if q > 0:
                    return q
                if rtt_ms < 50:   return 100
                if rtt_ms < 100:  return 90
                if rtt_ms < 200:  return 80
                if rtt_ms < 500:  return 60
                if rtt_ms < 1000: return 40
                return 20
            with self.lock:
                return max(0, self.signal_quality - 5)
        except Exception as e:
            logger.debug(f"HTTP quality ошибка: {e}")
            with self.lock:
                return max(0, self.signal_quality - 10)

    def get_connection_status(self):
        with self.lock:
            j2534 = self._j2534
            sq    = self.signal_quality

        # J2534: опрашиваем объект напрямую, без HTTP
        if j2534 is not None:
            alive = j2534.is_connected()
            if not alive:
                with self.lock:
                    self._j2534 = None
            return {
                "connected":    alive,
                "protocol":     "J2534/ISO15765",
                "port":         "J2534",
                "signal_quality": sq if alive else 0,
            }

        try:
            response = requests.get(f"{self.base_url}/connection/status",
                                   timeout=3,
                                   headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                data = response.json()
                with self.lock:
                    data["signal_quality"] = self.signal_quality
                return data
        except requests.exceptions.ConnectionError:
            logger.debug("Ошибка подключения к серверу")
        except requests.exceptions.Timeout:
            logger.debug("Таймаут при запросе статуса")
        except Exception as e:
            logger.debug(f"Ошибка получения статуса: {e}")

        return {"connected": False, "protocol": "None", "port": "None"}

    def clear_ecu_info(self):
        try:
            self.ecu_data_cleared.emit()
        except Exception:
            pass

    def scan_ports(self):
        try:
            logger.info("Сканирование портов...")
            response = requests.get(f"{self.base_url}/ports/scan_all",
                                   timeout=10,
                                   headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                ports = response.json()
                logger.info(f"Найдено портов: {len(ports)}")
                self.ports_update.emit(ports)
                return ports
        except requests.exceptions.ConnectionError:
            logger.error("Сервер недоступен")
        except Exception as e:
            logger.error(f"Ошибка сканирования портов: {e}")
        return []

    def scan_bluetooth_devices(self):
        """Сканирование Bluetooth устройств.

        Метод 1 — COM-порты с 'bluetooth'/'rfcomm' в описании (уже спаренные адаптеры).
        Метод 2 — PyBluez discovery (обнаружение новых устройств, если библиотека установлена).
        Возвращает список dict с полями: name, description, type, address, paired.
        """
        bt_ports = []

        # ── Метод 1: спаренные BT-устройства как COM-порты ───────────────────
        try:
            import serial.tools.list_ports
            for port in serial.tools.list_ports.comports():
                desc = (port.description or "").lower()
                hwid = (port.hwid or "").lower()
                if "bluetooth" in desc or "rfcomm" in desc or "bluetooth" in hwid:
                    bt_ports.append({
                        "name":        port.device,
                        "description": port.description or "Bluetooth COM-порт",
                        "type":        "bluetooth",
                        "address":     port.device,
                        "paired":      True,
                    })
            logger.info(f"BT COM-портов найдено: {len(bt_ports)}")
        except Exception as e:
            logger.warning(f"Ошибка сканирования BT COM: {e}")

        # ── Метод 2: PyBluez discovery (необязательная зависимость) ──────────
        try:
            import bluetooth  # type: ignore
            logger.info("PyBluez: поиск устройств (~8 сек)...")
            nearby = bluetooth.discover_devices(duration=8, lookup_names=True,
                                                lookup_class=True)
            for addr, name, _ in nearby:
                if not any(p.get("address") == addr for p in bt_ports):
                    bt_ports.append({
                        "name":        addr,
                        "description": name or f"BT {addr}",
                        "type":        "bluetooth",
                        "address":     addr,
                        "paired":      False,
                    })
            logger.info(f"PyBluez нашёл устройств: {len(nearby)}")
        except ImportError:
            logger.debug("bluetooth (PyBluez) не установлен — пропуск discovery")
        except Exception as e:
            logger.warning(f"Ошибка PyBluez discovery: {e}")

        logger.info(f"Итого BT устройств: {len(bt_ports)}")
        return bt_ports

    def scan_j2534_devices(self) -> list:
        """Сканирует реестр Windows на наличие J2534 PassThru устройств."""
        try:
            from j2534_passthru import scan_j2534_registry
            devices = scan_j2534_registry()
            logger.info(f"J2534 устройств найдено: {len(devices)}")
            return devices
        except Exception as e:
            logger.warning(f"Ошибка сканирования J2534: {e}")
            return []

    def connect_to_j2534(self, port_info: dict) -> dict:
        """Открывает J2534 PassThru соединение, минуя C++ бэкенд."""
        from j2534_passthru import create_j2534_adapter

        dll_path = port_info.get("dll_path", "")
        if not dll_path:
            return {"success": False, "error": "Не указан путь к J2534 DLL"}

        try:
            adapter = create_j2534_adapter(dll_path)

            if not adapter.open():
                return {"success": False,
                        "error": f"Не удалось открыть {port_info.get('description')}. "
                                 "Проверьте, что адаптер подключён к USB."}

            if not adapter.auto_connect():
                adapter.close()
                return {"success": False,
                        "error": "Не удалось подключиться к шине автомобиля. "
                                 "Проверьте, что кабель OBD вставлен в разъём."}

            adapter.setup_obd_filter()

            vin = adapter.read_vin()

            with self.lock:
                self._j2534 = adapter
                self._is_connected = True
                self.connection_start_time = datetime.now()
                self.reconnect_attempts = 0

            logger.info(f"J2534 подключено: {port_info.get('description')}, VIN={vin!r}")
            return {
                "success":  True,
                "protocol": "J2534/ISO15765",
                "port":     port_info.get("name", ""),
                "vin":      vin,
            }
        except Exception as e:
            logger.error(f"Исключение J2534 connect: {e}")
            return {"success": False, "error": str(e)}

    def connect_to_port(self, port_info):
        # J2534 PassThru — работает напрямую через DLL, без HTTP-бэкенда
        if port_info.get("type") == "j2534":
            return self.connect_to_j2534(port_info)

        try:
            logger.info(f"Подключение к порту: {port_info.get('name')}, тип: {port_info.get('type')}")

            status = self.get_connection_status()
            if status.get("connected", False):
                logger.info("Уже подключены, отключаемся...")
                self.disconnect()
                time.sleep(2)

            response = requests.post(f"{self.base_url}/connect",
                                    json=port_info,
                                    timeout=180,
                                    headers={'Connection': 'keep-alive'})

            if response.status_code == 200:
                result = response.json()
                logger.info(f"Ответ сервера: {result}")

                if result.get("success"):
                    with self.lock:
                        self._is_connected = True
                        self.connection_start_time = datetime.now()
                        self.reconnect_attempts = 0
                    logger.info("Подключение успешно")
                    
                    # Даем время на стабилизацию соединения
                    time.sleep(1)
                else:
                    logger.warning(f"Ошибка подключения: {result.get('error', 'Неизвестная ошибка')}")

                return result
            else:
                logger.error(f"HTTP ошибка: {response.status_code}")
                return {"success": False, "error": f"HTTP {response.status_code}"}

        except requests.exceptions.ConnectionError as e:
            logger.error(f"Ошибка подключения к серверу: {e}")
            return {"success": False, "error": "Server connection failed"}
        except requests.exceptions.Timeout:
            logger.error("Таймаут подключения")
            return {"success": False, "error": "Connection timeout"}
        except Exception as e:
            logger.error(f"Исключение при подключении: {e}")
            logger.error(traceback.format_exc())
            return {"success": False, "error": str(e)}

    def disconnect(self):
        try:
            logger.info("Отключение...")

            with self.lock:
                j2534 = self._j2534
                self._j2534 = None

            if j2534 is not None:
                # J2534: закрываем DLL напрямую
                try:
                    j2534.disconnect()
                except Exception:
                    pass
                success = True
            else:
                # HTTP-бэкенд
                try:
                    response = requests.post(f"{self.base_url}/disconnect",
                                             timeout=3,
                                             headers={'Connection': 'keep-alive'})
                    success = response.status_code == 200
                except Exception:
                    success = True  # Даже если сервер не ответил — считаем отключением

            with self.lock:
                self._is_connected = False
                self.connection_start_time = None
                self.signal_quality = 0
                self.current_connection = {"connected": False, "protocol": "None", "port": "None"}

            self.clear_ecu_info()
            self.connection_status_changed.emit(self.current_connection)
            self.connection_quality_changed.emit(0)

            if success:
                logger.info("Отключение успешно")
                return True
            else:
                logger.warning("Ошибка отключения")
                return False

        except Exception as e:
            logger.error(f"Ошибка отключения: {e}")
            with self.lock:
                self._is_connected = False
                self.connection_start_time = None
                self.signal_quality = 0
                self._j2534 = None
            self.clear_ecu_info()
            return False

    def read_error_codes(self):
        with self.lock:
            if not self._is_connected:
                return {"errors": [], "count": 0}
            j2534 = self._j2534

        if j2534 is not None:
            try:
                errors = j2534.read_dtc()
                return {"errors": errors, "count": len(errors)}
            except Exception as e:
                logger.error(f"J2534 read_dtc: {e}")
                return {"errors": [], "count": 0}

        try:
            response = requests.get(f"{self.base_url}/errors/read",
                                   timeout=5,
                                   headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                data = response.json()
                logger.info(f"Получено ошибок: {data.get('count', 0)}")
                return data
        except Exception as e:
            logger.error(f"Ошибка чтения ошибок: {e}")
        return {"errors": [], "count": 0}

    def get_error_description(self, dtc_code):
        if dtc_code in self.dtc_cache:
            return self.dtc_cache[dtc_code]

        try:
            response = requests.get(f"{self.base_url}/dtc/{dtc_code}",
                                   timeout=3,
                                   headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                data = response.json()
                self.dtc_cache[dtc_code] = data
                return data
        except Exception as e:
            logger.debug(f"Ошибка получения описания DTC {dtc_code}: {e}")
        return {"code": dtc_code, "description": "Unknown DTC code"}

    def get_all_dtc_codes(self):
        try:
            response = requests.get(f"{self.base_url}/dtc/all",
                                   timeout=5,
                                   headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                data = response.json()
                for dtc in data:
                    if 'code' in dtc:
                        self.dtc_cache[dtc['code']] = dtc
                return data
        except Exception as e:
            logger.error(f"Ошибка получения всех DTC: {e}")
        return []

    def search_dtc_codes(self, query):
        try:
            response = requests.get(f"{self.base_url}/dtc/search?query={quote(query)}",
                                   timeout=3,
                                   headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                return response.json()
        except Exception as e:
            logger.error(f"Ошибка поиска DTC: {e}")
        return []

    def clear_errors(self):
        with self.lock:
            if not self._is_connected:
                return False
            j2534 = self._j2534

        if j2534 is not None:
            try:
                return j2534.clear_dtc()
            except Exception as e:
                logger.error(f"J2534 clear_dtc: {e}")
                return False

        try:
            response = requests.post(f"{self.base_url}/errors/clear",
                                    timeout=5,
                                    headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                data = response.json()
                logger.info("Ошибки очищены")
                return data.get("success", False)
        except Exception as e:
            logger.error(f"Ошибка очистки ошибок: {e}")
        return False

    def get_realtime_data(self):
        with self.lock:
            if not self._is_connected:
                return {}
            j2534 = self._j2534

        if j2534 is not None:
            try:
                return j2534.read_realtime_data()
            except Exception as e:
                logger.debug(f"J2534 realtime_data: {e}")
                return {}

        try:
            response = requests.get(f"{self.base_url}/live/data",
                                   timeout=2,
                                   headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                return response.json()
        except Exception as e:
            logger.debug(f"Ошибка получения live данных: {e}")
        return {}

    def get_connection_time(self):
        with self.lock:
            if self.connection_start_time:
                elapsed = datetime.now() - self.connection_start_time
                return str(elapsed).split('.')[0]
        return "00:00:00"

    def get_ecu_info(self):
        with self.lock:
            if not self._is_connected:
                return {}
            j2534 = self._j2534

        if j2534 is not None:
            try:
                # Данные адаптера
                info = {"Интерфейс": "J2534 PassThru"}
                ver = j2534.read_version()
                if ver:
                    info["Прошивка адаптера"] = ver.get("firmware", "-")
                    info["Версия DLL"]        = ver.get("dll", "-")
                    info["Версия API"]        = ver.get("api", "-")

                # Данные ЭБУ (VIN, калибровка, имя блока)
                ecu = j2534.read_ecu_info()
                info.update(ecu)

                return info
            except Exception as e:
                logger.debug(f"J2534 get_ecu_info: {e}")
                return {"Интерфейс": "J2534 PassThru"}

        try:
            response = requests.get(f"{self.base_url}/ecu/info",
                                   timeout=3,
                                   headers={'Connection': 'keep-alive'})
            if response.status_code == 200:
                raw = response.json()
                # Добавляем информацию об интерфейсе (ELM327 / HTTP-бэкенд)
                with self.lock:
                    conn = self.current_connection
                raw["Интерфейс"] = f"ELM327 / {conn.get('protocol', 'OBD-II')}"
                raw["Протокол"]  = conn.get("protocol", "—")
                return raw
        except Exception as e:
            logger.debug(f"Ошибка получения информации об ЭБУ: {e}")
        return {}