import os
import json
from datetime import datetime

# Конфигурация приложения
class Config:
    # Пути
    BASE_DIR = os.path.dirname(os.path.dirname(__file__))
    DATA_DIR = os.path.join(BASE_DIR, "data")
    LOGS_DIR = os.path.join(DATA_DIR, "logs")
    BACKUP_DIR = os.path.join(DATA_DIR, "ecu_backups")
    EXPORT_DIR = os.path.join(DATA_DIR, "exports")
    
    # Настройки сервера
    DEFAULT_HOST = "localhost"
    DEFAULT_PORT = 8080
    
    # Настройки мониторинга
    MONITOR_INTERVAL = 2
    QUALITY_INTERVAL = 5
    
    # Настройки базы данных
    DB_PATH = os.path.join(BASE_DIR, "ecu_diagnostic.db")
    
    # Поддерживаемые протоколы
    SUPPORTED_PROTOCOLS = ["OBD-II", "CAN", "KWP2000", "UDS", "J1850", "ISO9141"]
    
    # Цвета для интерфейса
    COLORS = {
        "success": "#4CAF50",
        "warning": "#FFC107",
        "error": "#F44336",
        "info": "#2196F3",
        "background": "#353535",
        "text": "#FFFFFF",
        "text_secondary": "#AAAAAA"
    }
    
    @classmethod
    def ensure_dirs(cls):
        """Создание необходимых директорий"""
        for dir_path in [cls.DATA_DIR, cls.LOGS_DIR, cls.BACKUP_DIR, cls.EXPORT_DIR]:
            os.makedirs(dir_path, exist_ok=True)
    
    @classmethod
    def get_log_filename(cls, prefix="app"):
        """Получение имени файла лога с датой"""
        return os.path.join(cls.LOGS_DIR, f"{prefix}_{datetime.now().strftime('%Y%m%d')}.log")
    
    @classmethod
    def save_settings(cls, settings):
        """Сохранение настроек"""
        settings_file = os.path.join(cls.DATA_DIR, "settings.json")
        try:
            with open(settings_file, 'w', encoding='utf-8') as f:
                json.dump(settings, f, indent=2, ensure_ascii=False)
            return True
        except Exception as e:
            print(f"Ошибка сохранения настроек: {e}")
            return False
    
    @classmethod
    def load_settings(cls):
        """Загрузка настроек"""
        settings_file = os.path.join(cls.DATA_DIR, "settings.json")
        try:
            if os.path.exists(settings_file):
                with open(settings_file, 'r', encoding='utf-8') as f:
                    return json.load(f)
        except Exception as e:
            print(f"Ошибка загрузки настроек: {e}")
        return {}

# Создаем необходимые директории при импорте
Config.ensure_dirs()