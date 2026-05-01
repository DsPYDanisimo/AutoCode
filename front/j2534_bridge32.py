#!/usr/bin/env python
"""
j2534_bridge32.py — J2534 DLL bridge для 32-bit Python.

Запускается как subprocess из 64-bit основного приложения когда J2534 DLL
оказывается 32-bit (monpj432.dll и подобные).

Протокол:
  stdin  — JSON-строки: {"method": "...", "args": [...], "kwargs": {...}}
  stdout — JSON-строки: {"ok": true/false, "result": ..., "error": "..."}

Первая строка stdout — {"ok": true, "ready": true}  (сигнал готовности).

Реализация вынесена в j2534_passthru.J2534PassThru — изменения достаточно
вносить только в один файл.
"""
import sys
import json
import logging
import os

# Логи пишем в stderr (который наследует основной процесс — попадают в его лог-файл)
logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    stream=sys.stderr,
)

# Добавляем директорию скрипта в sys.path, чтобы найти j2534_passthru.py
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    from j2534_passthru import J2534PassThru
except Exception as e:
    print(json.dumps({"ok": False, "error": f"Импорт j2534_passthru: {e}"}), flush=True)
    sys.exit(1)


def main():
    if len(sys.argv) < 2:
        print(json.dumps({"ok": False, "error": "Не указан путь к DLL"}), flush=True)
        sys.exit(1)

    dll_path = sys.argv[1]

    try:
        worker = J2534PassThru(dll_path)
    except Exception as e:
        print(json.dumps({"ok": False, "error": f"Инициализация J2534PassThru: {e}"}), flush=True)
        sys.exit(1)

    # Сигнализируем основному процессу, что bridge готов
    print(json.dumps({"ok": True, "ready": True}), flush=True)

    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue

        try:
            cmd = json.loads(line)
        except json.JSONDecodeError as e:
            print(json.dumps({"ok": False, "error": f"JSON: {e}"}), flush=True)
            continue

        method = cmd.get("method")
        args   = cmd.get("args",   [])
        kwargs = cmd.get("kwargs", {})

        if method == "exit":
            try:
                worker.disconnect()
            except Exception:
                pass
            break

        fn = getattr(worker, method, None)
        if fn is None:
            print(json.dumps({"ok": False, "error": f"Нет метода: {method}"}), flush=True)
            continue

        try:
            result = fn(*args, **kwargs)
            print(json.dumps({"ok": True, "result": result}), flush=True)
        except Exception as e:
            print(json.dumps({"ok": False, "error": str(e)}), flush=True)


if __name__ == "__main__":
    main()
