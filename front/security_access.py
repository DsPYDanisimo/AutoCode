"""
security_access.py — интерфейс поставщика ключа UDS SecurityAccess (0x27).

Алгоритм seed→key — фирменная тайна каждого производителя ЭБУ и НЕ входит
в этот проект. NoKeyProvider (используется по умолчанию) явно сообщает об
отсутствии алгоритма вместо того, чтобы подставлять случайный/угаданный ключ —
не рискуем "залочить" ЭБУ неверными попытками (ExceedNumberOfAttempts, NRC 0x36)
и не делаем вид, что разблокировка работает, когда это не так.

Чтобы подключить реальный алгоритм (из легального источника — лицензированного
инструмента, официальной документации производителя и т.п.), реализуйте
compute_key() в своём классе-наследнике SecurityKeyProvider и передайте его
в ECUReadBackupDialog/вызывающий код.
"""

from abc import ABC, abstractmethod


class SecurityAccessError(Exception):
    """Ошибка расчёта или применения ключа Security Access."""


class SecurityKeyProvider(ABC):
    """Поставщик ключа для UDS SecurityAccess (0x27)."""

    @abstractmethod
    def compute_key(self, ecu_id: str, access_level: int, seed: bytes) -> bytes:
        """
        Вычисляет key по seed.

        ecu_id — произвольный идентификатор ЭБУ/адреса (например "0x7E0" или
        "VIN:...:ECM"), чтобы провайдер мог выбрать алгоритм под конкретный
        блок управления, если их несколько.
        access_level — уровень доступа, на который был запрошен seed
        (нечётное число, например 0x01, 0x11).
        seed — байты seed, полученные от ЭБУ.

        Возвращает вычисленный key. Бросает SecurityAccessError, если ключ
        вычислить невозможно (нет алгоритма для этого ЭБУ и т.п.).
        """
        raise NotImplementedError


class NoKeyProvider(SecurityKeyProvider):
    """Провайдер по умолчанию — явная ошибка вместо угаданного ключа."""

    def compute_key(self, ecu_id: str, access_level: int, seed: bytes) -> bytes:
        raise SecurityAccessError(
            f"Не настроен провайдер ключа Security Access для {ecu_id} "
            f"(уровень {access_level:#x}). Алгоритм seed→key специфичен для "
            f"производителя ЭБУ и должен быть подключён отдельно "
            f"(см. front/security_access.py: SecurityKeyProvider)."
        )


def unlock_security(transport, ecu_id: str, access_level: int,
                     provider: SecurityKeyProvider, ecu_addr: int = 0x7E0) -> bool:
    """
    Полный цикл SecurityAccess поверх любого транспорта, у которого есть методы
    request_seed(access_level, ecu_addr) и send_key(access_level, key_hex, ecu_addr)
    — подходят и J2534PassThru, и J2534PassThruBridge (см. j2534_passthru.py).

    Провайдер ключа намеренно вызывается ЗДЕСЬ, а не внутри транспорта: у моста
    J2534PassThruBridge транспорт живёт в отдельном 32-битном процессе и общается
    через JSON, куда нельзя передать произвольный Python-объект вроде провайдера.
    """
    seed_hex = transport.request_seed(access_level, ecu_addr=ecu_addr)
    if seed_hex is None:
        return False
    if seed_hex == "":
        # Пустой seed — ЭБУ уже разблокирован на этом уровне, ключ не нужен.
        return True

    key = provider.compute_key(ecu_id, access_level, bytes.fromhex(seed_hex))
    return transport.send_key(access_level + 1, key.hex(), ecu_addr=ecu_addr)
