from PyQt5.QtCore import QThread, pyqtSignal
import serial.tools.list_ports
import time
import logging
import os
from datetime import datetime

# Настройка логирования
LOG_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "logs")
os.makedirs(LOG_DIR, exist_ok=True)

log_filename = os.path.join(LOG_DIR, f"ecu_monitor_{datetime.now().strftime('%Y%m%d')}.log")
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler(log_filename, encoding='utf-8'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)


class ECUMonitorThread(QThread):
    """Поток для мониторинга подключения к ЭБУ"""
    
    connection_detected = pyqtSignal(dict)  # {connected: bool, protocol: str, port: str}
    ports_updated = pyqtSignal(list)
    error_occurred = pyqtSignal(str)
    monitor_status = pyqtSignal(str)
    
    def __init__(self):
        super().__init__()
        self.running = True
        self.scan_interval = 3  # Интервал сканирования в секундах
        self.last_known_ports = []
        self.connection_attempts = 0
        self.max_connection_attempts = 3
        self.supported_protocols = ["OBD-II", "CAN", "KWP2000", "UDS", "J1850", "ISO9141"]
        self.last_connected_status = False
        logger.info("ECUMonitorThread инициализирован")
        
    def run(self):
        """Основной цикл мониторинга"""
        logger.info("Мониторинг запущен")
        while self.running:
            try:
                # Сканирование портов
                ports = self.scan_serial_ports()
                
                # Проверяем, изменился ли список портов
                if ports != self.last_known_ports:
                    logger.info(f"Список портов изменился: {len(ports)} портов")
                    self.ports_updated.emit(ports)
                    self.last_known_ports = ports
                
                # Проверка подключения
                connection_status = self.check_ecu_connection(ports)
                
                # Если есть изменения в статусе подключения
                current_connected = connection_status.get("connected", False)
                if current_connected and not self.last_connected_status:
                    logger.info(f"Подключение обнаружено на {connection_status.get('port')}")
                    self.connection_attempts = 0
                elif not current_connected and self.last_connected_status:
                    logger.info("Подключение потеряно")
                self.last_connected_status = current_connected  # обновляем состояние

                self.connection_detected.emit(connection_status)
                
                # Обновляем статус
                if connection_status.get("connected"):
                    self.monitor_status.emit(f"Подключено к {connection_status.get('port')} ({connection_status.get('protocol')})")
                else:
                    self.monitor_status.emit("Ожидание подключения...")
                
            except Exception as e:
                logger.error(f"Ошибка в цикле мониторинга: {e}")
                self.error_occurred.emit(str(e))
            
            # Пауза перед следующим сканированием
            for _ in range(self.scan_interval * 10):
                if not self.running:
                    break
                time.sleep(0.1)
        
        logger.info("Мониторинг остановлен")
              
    def scan_serial_ports(self):
        """Сканирование доступных COM портов с расширенной информацией"""
        ports = []
        try:
            available_ports = serial.tools.list_ports.comports()
            
            for port in available_ports:
                port_info = {
                    "device": port.device,
                    "description": port.description,
                    "manufacturer": port.manufacturer if port.manufacturer else "Unknown",
                    "hwid": port.hwid,
                    "vid": port.vid,
                    "pid": port.pid,
                    "serial_number": port.serial_number,
                    "location": port.location,
                    "interface": port.interface,
                    "is_obd": self.is_obd_device(port)
                }
                
                # Определяем тип порта для фильтрации
                description_lower = port.description.lower() if port.description else ""
                if "bluetooth" in description_lower or "bt" in description_lower:
                    port_info["type"] = "bluetooth"
                elif "usb" in description_lower:
                    port_info["type"] = "usb"
                else:
                    port_info["type"] = "serial"
                
                ports.append(port_info)
                
                logger.debug(f"Найден порт: {port.device} - {port.description}")
                
        except Exception as e:
            logger.error(f"Ошибка сканирования портов: {e}")
        
        return ports
    
    def is_obd_device(self, port):
        """Проверка, является ли устройство OBD адаптером"""
        # Ключевые слова для OBD-II адаптеров
        obd_keywords = ["obd", "elm327", "vgate", "can", "diagnostic", "obd2", "obdii", "kkl", "vagcom"]
        
        description = port.description.lower() if port.description else ""
        manufacturer = port.manufacturer.lower() if port.manufacturer else ""
        hwid = port.hwid.lower() if port.hwid else ""
        
        for keyword in obd_keywords:
            if keyword in description or keyword in manufacturer or keyword in hwid:
                return True
        
        # Проверка по VID/PID для известных адаптеров
        known_obd_vids = {
            0x0403: "FTDI",      # FTDI chips
            0x067b: "Prolific",   # Prolific PL2303
            0x1a86: "Qinheng",    # CH340/CH341
            0x10c4: "Silicon Labs" # CP210x
        }
        
        if port.vid in known_obd_vids:
            return True
        
        return False
      
    def check_ecu_connection(self, ports):
        """Проверка подключения к ЭБУ с поддержкой разных протоколов"""
        
        # Если есть COM порты с OBD адаптерами
        for port_info in ports:
            if port_info.get("is_obd"):
                port_name = port_info["device"]
                
                # Проверка наличия ELM327 или другого адаптера
                try:
                    # Попытка открыть порт для проверки
                    ser = serial.Serial(port_name, 115200, timeout=1)
                    ser.close()
                    
                    # Имитация детектирования протокола
                    detected_protocol = self.detect_protocol_from_description(port_info)
                    
                    self.connection_attempts += 1
                    
                    if self.connection_attempts >= self.max_connection_attempts:
                        self.connection_attempts = 0
                    
                    return {
                        "connected": True,
                        "port": port_name,
                        "protocol": detected_protocol,
                        "ecu_type": f"OBD Adapter ({port_info.get('manufacturer', 'Unknown')})",
                        "vin": "SIMULATED_VIN_123456",
                        "port_info": port_info
                    }
                    
                except Exception as e:
                    logger.debug(f"Не удалось открыть порт {port_name}: {e}")
                    
        # Имитация подключения для демонстрации
        if ports:
            # Для демонстрации используем первый доступный порт
            port_info = ports[0]
            detected_protocol = self.detect_protocol_from_description(port_info)
            
            return {
                "connected": True,
                "port": port_info["device"],
                "protocol": detected_protocol,
                "ecu_type": "Simulated ECU",
                "vin": "SIMULATED_VIN_123456",
                "port_info": port_info
            }
        
        # Если нет портов или адаптеров
        return {
            "connected": False,
            "port": "NONE",
            "protocol": "NONE",
            "ecu_type": "",
            "vin": ""
        }
    
    def detect_protocol_from_description(self, port_info):
        """Определение вероятного протокола по описанию устройства"""
        description = f"{port_info.get('description', '')} {port_info.get('manufacturer', '')}".lower()
        
        protocol_scores = {}
        for protocol in self.supported_protocols:
            protocol_scores[protocol] = 0
        
        # Анализ ключевых слов
        if "can" in description:
            protocol_scores["CAN"] += 3
        if "obd" in description:
            protocol_scores["OBD-II"] += 3
        if "kwp" in description or "2000" in description:
            protocol_scores["KWP2000"] += 3
        if "uds" in description:
            protocol_scores["UDS"] += 3
        if "j1850" in description or "pwm" in description or "vpw" in description:
            protocol_scores["J1850"] += 2
        if "iso9141" in description or "9141" in description:
            protocol_scores["ISO9141"] += 2
        if "elm327" in description:
            # ELM327 поддерживает несколько протоколов
            for protocol in ["OBD-II", "CAN", "J1850", "ISO9141"]:
                protocol_scores[protocol] += 1
        
        # Выбираем протокол с наибольшим score
        best_protocol = max(protocol_scores, key=protocol_scores.get)
        
        if protocol_scores[best_protocol] > 0:
            return best_protocol
        else:
            return "OBD-II"  # По умолчанию
    
    def force_scan(self):
        """Принудительное сканирование"""
        logger.info("Принудительное сканирование портов")
        ports = self.scan_serial_ports()
        self.ports_updated.emit(ports)
        return ports
      
    def stop(self):
        """Остановка мониторинга"""
        logger.info("Остановка мониторинга")
        self.running = False
        self.wait()