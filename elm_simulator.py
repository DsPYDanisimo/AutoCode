#!/usr/bin/env python3
"""
ELM327 TCP Simulator v2.0
Полная симуляция ЭБУ автомобиля через TCP.
Протокол: TCP, порт 35000
Поддерживаемые режимы: OBD-II (Mode 01-09), KWP2000, UDS
"""

import socket
import threading
import time
import math
import sys
import traceback
import random
from datetime import datetime


class VehicleState:
    """Симуляция состояния автомобиля с реалистичными параметрами."""

    # DTC коды для симуляции (raw_hex, code_str, description)
    # raw_hex = 2 байта OBD-II: byte1 = тип+разряды, byte2 = последние 2 цифры
    ACTIVE_DTCS = [
        ("0300", "P0300", "Random/Multiple Cylinder Misfire Detected"),
        ("0171", "P0171", "System Too Lean (Bank 1)"),
        ("0420", "P0420", "Catalyst System Efficiency Below Threshold"),
        ("0441", "P0441", "Evaporative Emission Control System Incorrect Purge Flow"),
        ("0113", "P0113", "Intake Air Temperature Circuit High Input"),
        ("0562", "P0562", "System Voltage Low"),
    ]

    VIN = "1HGBH41JXMN109186"  # 17 символов

    def __init__(self):
        self.engine_on = True
        self.start_time = time.time()
        self.dtcs_cleared = False

        # Начальное состояние — холодный двигатель
        self._coolant = 22.0
        self._intake = 18.0
        self._rpm_idle_target = 800.0
        self._rpm_current = 1500.0  # прогрев

        # Запускаем поток обновления физики
        self._lock = threading.Lock()
        self._state_thread = threading.Thread(target=self._update_loop, daemon=True)
        self._state_thread.start()

    def _update_loop(self):
        while self.engine_on:
            elapsed = time.time() - self.start_time
            warmup = min(1.0, elapsed / 90.0)   # прогрев за 90 сек симуляции

            with self._lock:
                # Температура ОЖ: 22 → 90°C
                self._coolant = 22.0 + warmup * 68.0 + 1.5 * math.sin(elapsed * 0.08)
                # Температура впуска: чуть растёт
                self._intake = 18.0 + warmup * 8.0 + 0.5 * math.sin(elapsed * 0.05)
                # Целевые обороты холостого хода: на прогреве выше
                self._rpm_idle_target = 800.0 + (1 - warmup) * 400.0

            time.sleep(0.3)

    # ---------- Параметры ----------

    def rpm(self):
        with self._lock:
            base = self._rpm_idle_target
        t = time.time()
        cycle = (t % 90) / 90.0
        if cycle < 0.25:
            driving_rpm = 0.0
        elif cycle < 0.55:
            driving_rpm = 2200 * (cycle - 0.25) / 0.30
        elif cycle < 0.75:
            driving_rpm = 2200.0
        else:
            driving_rpm = 2200 * (1 - (cycle - 0.75) / 0.25)
        raw = base + driving_rpm + 80 * math.sin(t * 1.1) + 40 * math.sin(t * 2.3)
        return max(600.0, raw + random.gauss(0, 15))

    def speed(self):
        t = time.time()
        cycle = (t % 90) / 90.0
        if cycle < 0.25:
            return 0.0
        elif cycle < 0.55:
            spd = 80.0 * (cycle - 0.25) / 0.30
        elif cycle < 0.75:
            spd = 80.0
        else:
            spd = 80.0 * (1 - (cycle - 0.75) / 0.25)
        return max(0.0, spd + 3 * math.sin(t * 0.4) + random.gauss(0, 1))

    def coolant_temp(self):
        with self._lock:
            return self._coolant

    def intake_temp(self):
        with self._lock:
            return self._intake

    def throttle(self):
        spd = self.speed()
        if spd < 1.0:
            return 0.0
        base = 10.0 + spd * 0.35
        return min(100.0, base + 5 * math.sin(time.time() * 0.5) + random.gauss(0, 1))

    def maf(self):
        # г/с: примерно пропорционально RPM и дросселю
        r = self.rpm()
        th = self.throttle()
        base = 3.0 + r * 0.002 + th * 0.05
        return max(2.0, base + random.gauss(0, 0.3))

    def fuel_pressure(self):
        # Давление топлива в kPa (gauge), типично 300-420 kPa
        t = time.time()
        return 380.0 + 20 * math.sin(t * 0.25) + random.gauss(0, 3)

    def map_pressure(self):
        # Manifold Absolute Pressure (kPa)
        th = self.throttle()
        return 30.0 + th * 0.7 + random.gauss(0, 0.5)

    def voltage(self):
        # Напряжение бортовой сети
        t = time.time()
        return 14.1 + 0.3 * math.sin(t * 0.1) + random.gauss(0, 0.03)

    def engine_load(self):
        th = self.throttle()
        return min(100.0, 5.0 + th * 1.1 + random.gauss(0, 1))

    def o2_voltage_mv(self):
        t = time.time()
        return max(50, min(950, 450 + 380 * math.sin(t * 2.2) + random.gauss(0, 20)))

    def fuel_trim(self):
        return 2.0 + 1.0 * math.sin(time.time() * 0.6) + random.gauss(0, 0.3)

    def run_time_sec(self):
        return int(time.time() - self.start_time)

    # ---------- DTC ----------

    def get_dtcs(self):
        if self.dtcs_cleared:
            return []
        return self.ACTIVE_DTCS

    def clear_dtcs(self):
        self.dtcs_cleared = True

    def dtc_count(self):
        return len(self.get_dtcs())


class ELM327Simulator:
    """Полный симулятор ELM327 по TCP."""

    def __init__(self, port=35000):
        self.port = port
        self.running = True
        self.connected_clients = []
        self.client_threads = []
        self.server_socket = None
        self.vehicle = VehicleState()
        self.keep_alive_interval = 15

    # ------------------------------------------------------------------ #
    #  Управление клиентами                                               #
    # ------------------------------------------------------------------ #

    def handle_client(self, client_socket, address):
        ts = lambda: datetime.now().strftime('%H:%M:%S')
        print(f"[{ts()}] + клиент {address}")
        self.connected_clients.append(client_socket)

        # Состояние сессии (на каждого клиента своё)
        state = {
            'echo': False,
            'headers': True,
            'linefeeds': False,
            'spaces': True,
            'protocol': '0',
        }

        client_socket.settimeout(5)
        last_activity = time.time()
        buf = ""

        while self.running:
            try:
                if time.time() - last_activity > self.keep_alive_interval:
                    try:
                        client_socket.send(b"\r>")
                        last_activity = time.time()
                    except Exception:
                        break

                client_socket.settimeout(1)
                try:
                    chunk = client_socket.recv(1024).decode(errors='replace')
                    if not chunk:
                        break
                    last_activity = time.time()
                    buf += chunk

                    while '\r' in buf:
                        idx = buf.index('\r')
                        raw_cmd = buf[:idx].strip()
                        buf = buf[idx + 1:]
                        if not raw_cmd:
                            continue

                        print(f"[{ts()}] → {raw_cmd!r}")
                        response = self._dispatch(raw_cmd, state)
                        # ATS0: strip spaces from OBD responses (real ELM327 behaviour)
                        _up = raw_cmd.strip().upper().replace(' ', '')
                        if not state.get('spaces', True) and not _up.startswith('AT') and not _up.startswith('ST'):
                            response = response.replace(' ', '')
                        out = (response + "\r\n>").encode()
                        print(f"[{ts()}] ← {response[:60]!r}")
                        client_socket.send(out)

                except socket.timeout:
                    continue

            except (ConnectionResetError, ConnectionAbortedError):
                break
            except Exception as e:
                print(f"[{ts()}] ERR {address}: {e}")
                break

        self._cleanup_client(client_socket, address)

    def _cleanup_client(self, sock, address):
        try:
            sock.close()
        except Exception:
            pass
        if sock in self.connected_clients:
            self.connected_clients.remove(sock)
        print(f"[{datetime.now().strftime('%H:%M:%S')}] - клиент {address}")

    # ------------------------------------------------------------------ #
    #  Диспетчер команд                                                   #
    # ------------------------------------------------------------------ #

    def _dispatch(self, raw, state):
        cmd = raw.strip().upper()
        # Убираем пробелы только для AT-команд и для нормализации OBD
        cmd_nsp = cmd.replace(' ', '')

        if cmd_nsp.startswith('AT'):
            return self._at(cmd_nsp[2:], state)
        if cmd_nsp.startswith('ST'):
            return "OK"   # STN chip команды — всегда OK
        return self._obd(cmd_nsp, state)

    # ------------------------------------------------------------------ #
    #  AT-команды                                                          #
    # ------------------------------------------------------------------ #

    def _at(self, sub, state):
        v = self.vehicle

        # --- Сброс ---
        if sub in ('Z', 'D', 'WS'):
            time.sleep(0.3)
            state.update(echo=False, headers=True, linefeeds=False, spaces=True, protocol='0')
            return "ELM327 v2.3"

        # --- Echo ---
        if sub == 'E0':
            state['echo'] = False;  return "OK"
        if sub == 'E1':
            state['echo'] = True;   return "OK"

        # --- Headers ---
        if sub == 'H0':
            state['headers'] = False; return "OK"
        if sub == 'H1':
            state['headers'] = True;  return "OK"

        # --- Linefeeds ---
        if sub == 'L0':
            state['linefeeds'] = False; return "OK"
        if sub == 'L1':
            state['linefeeds'] = True;  return "OK"

        # --- Spaces ---
        if sub == 'S0':
            state['spaces'] = False; return "OK"
        if sub == 'S1':
            state['spaces'] = True;  return "OK"

        # --- Protocol ---
        if sub.startswith('SP'):
            state['protocol'] = sub[2:]
            return "OK"
        if sub.startswith('TP'):
            state['protocol'] = sub[2:]
            return "OK"

        # --- Device info ---
        if sub == 'I':
            return "ELM327 v2.3"
        if sub == '@1':
            return "OBDII to RS232 Interpreter"
        if sub == '@2':
            return "ELM327_SIMULATOR_v2.0"
        if sub == '@3':
            return "ELM327"

        # --- Voltage ---
        if sub == 'RV':
            return f"{v.voltage():.2f}V"

        # --- Ignition ---
        if sub == 'IGN':
            return "ON"

        # --- Describe protocol ---
        if sub == 'DP':
            proto_names = {
                '0': 'AUTO, ISO 15765-4 CAN',
                '1': 'SAE J1850 PWM',
                '2': 'SAE J1850 VPW',
                '3': 'ISO 9141-2',
                '4': 'ISO 14230-4 KWP (5 baud init)',
                '5': 'ISO 14230-4 KWP (fast init)',
                '6': 'ISO 15765-4 CAN (11 bit, 500 kbaud)',
                '7': 'ISO 15765-4 CAN (29 bit, 500 kbaud)',
                '8': 'ISO 15765-4 CAN (11 bit, 250 kbaud)',
                '9': 'ISO 15765-4 CAN (29 bit, 250 kbaud)',
                'A': 'SAE J1939 CAN',
            }
            return proto_names.get(state['protocol'], 'AUTO')
        if sub == 'DPN':
            return state['protocol']

        # --- Timeout (ATST xx) ---
        if sub.startswith('ST'):
            return "OK"

        # --- Adaptive timing (ATAT0/1/2) ---
        if sub.startswith('AT') and len(sub) == 3 and sub[2] in '012':
            return "OK"

        # --- CAN-related ---
        for prefix in ('CSM', 'CRA', 'SH', 'CAF', 'CFC', 'CM', 'CF', 'CB', 'CTM', 'CEA', 'CP'):
            if sub.startswith(prefix):
                return "OK"

        # --- Monitor All (CAN sniffing) ---
        if sub == 'MA' or sub.startswith('MR') or sub.startswith('MT'):
            return self._can_sniff()

        # --- Misc ---
        if sub in ('BD', 'FE', 'PC', 'LP', 'SS'):
            return "OK"
        if sub.startswith('BRD') or sub.startswith('BRT'):
            return "OK"
        if sub.startswith('IFR') or sub.startswith('KW') or sub.startswith('PB'):
            return "OK"
        if sub.startswith('R') and len(sub) == 2:   # ATR0 / ATR1 (responses)
            return "OK"
        if sub.startswith('NL') or sub.startswith('AL'):
            return "OK"

        # Неизвестная AT-команда — реальный ELM327 обычно отвечает "?"
        print(f"  [?] AT{sub}")
        return "?"

    def _can_sniff(self):
        ids = [0x7E8, 0x7E0, 0x201, 0x360, 0x420, 0x630]
        lines = []
        for _ in range(random.randint(3, 7)):
            cid = random.choice(ids)
            data = [random.randint(0, 255) for _ in range(8)]
            lines.append(f"{cid:03X} " + " ".join(f"{b:02X}" for b in data))
        return "\r".join(lines)

    # ------------------------------------------------------------------ #
    #  OBD-команды                                                         #
    # ------------------------------------------------------------------ #

    def _obd(self, cmd, state):
        v = self.vehicle

        # Mode 01 — текущие данные
        if cmd.startswith('01') and len(cmd) >= 4:
            return self._m01(cmd[2:4])

        # Mode 02 — freeze frame (возвращаем те же значения)
        if cmd.startswith('02') and len(cmd) >= 4:
            r = self._m01(cmd[2:4])
            return r.replace('41', '42', 1) if r != 'NODATA' else 'NODATA'

        # Mode 03 — чтение DTC
        if cmd == '03':
            return self._m03()

        # Mode 04 — сброс DTC
        if cmd == '04':
            v.clear_dtcs()
            return "44"

        # Mode 07 — pending DTC
        if cmd == '07':
            if v.dtcs_cleared:
                return "47 00"
            return "47 01 03 00"

        # Mode 09 — информация об автомобиле
        if cmd.startswith('09') and len(cmd) >= 4:
            return self._m09(cmd[2:4])

        # KWP2000: Start Diagnostic Session (0x10)
        if cmd.startswith('10'):
            return "50 01 00 19 01 F4"

        # UDS: ECU Reset (0x11)
        if cmd in ('1101', '1103'):
            return "51 " + cmd[2:4]

        # KWP2000: Read DTC by status (0x13)
        if cmd.startswith('13'):
            return self._kwp_dtc()

        # KWP2000/UDS: Clear DTC (0x14)
        if cmd.startswith('14'):
            v.clear_dtcs()
            return "54"

        # UDS: Read DTC Information (0x19)
        if cmd.startswith('19'):
            return self._uds_dtc(cmd[2:4] if len(cmd) >= 4 else '02')

        # KWP2000: Read data by local id (0x21)
        if cmd.startswith('21') and len(cmd) >= 6:
            return self._kwp_read(cmd[2:6])

        # UDS: Read data by identifier (0x22)
        if cmd.startswith('22') and len(cmd) >= 6:
            return self._uds_read(cmd[2:6])

        # UDS: Security Access (0x27)
        if cmd.startswith('27'):
            return "67 01 12 34"

        # UDS: Write by identifier (0x2E)
        if cmd.startswith('2E') and len(cmd) >= 6:
            return "6E " + cmd[2:4] + " " + cmd[4:6]

        # UDS: Tester Present (0x3E)
        if cmd.startswith('3E'):
            return "7E 00"

        print(f"  [?] OBD {cmd}")
        return "?"

    # ------------------------------------------------------------------ #
    #  Mode 01 — PIDs                                                      #
    # ------------------------------------------------------------------ #

    def _m01(self, pid):
        v = self.vehicle
        pid = pid.upper()

        if pid == '00':
            # Поддерживаемые PIDs 01-20: 01,04,05,06,07,0A,0B,0C,0D,0E,0F,10,11,13,14,1C,1F
            return "41 00 BF FF B8 93"

        if pid == '01':
            dtc_n = v.dtc_count()
            mil = 0x81 if dtc_n > 0 else 0x00
            return f"41 01 {mil:02X} {dtc_n:02X} 00 00"

        if pid == '04':   # Engine load
            val = round(v.engine_load() * 2.55)
            return f"41 04 {val:02X}"

        if pid == '05':   # Coolant temp
            val = max(0, min(255, round(v.coolant_temp()) + 40))
            return f"41 05 {val:02X}"

        if pid == '06':   # Short term fuel trim B1
            val = round((v.fuel_trim() + 100) * 1.28)
            return f"41 06 {val:02X}"

        if pid == '07':   # Long term fuel trim B1
            val = round((1.5 + 100) * 1.28)
            return f"41 07 {val:02X}"

        if pid == '0A':   # Fuel pressure gauge (kPa)
            val = max(0, min(255, round(v.fuel_pressure() / 3)))
            return f"41 0A {val:02X}"

        if pid == '0B':   # MAP (kPa absolute)
            val = max(0, min(255, round(v.map_pressure())))
            return f"41 0B {val:02X}"

        if pid == '0C':   # RPM
            raw = max(0, min(65535, round(v.rpm() * 4)))
            return f"41 0C {(raw >> 8) & 0xFF:02X} {raw & 0xFF:02X}"

        if pid == '0D':   # Speed
            val = max(0, min(255, round(v.speed())))
            return f"41 0D {val:02X}"

        if pid == '0E':   # Ignition timing advance
            timing = 12.0 + 3.0 * math.sin(time.time() * 0.3)
            val = round((timing + 64) * 2)
            return f"41 0E {val:02X}"

        if pid == '0F':   # Intake air temp
            val = max(0, min(255, round(v.intake_temp()) + 40))
            return f"41 0F {val:02X}"

        if pid == '10':   # MAF (г/с * 100)
            raw = max(0, min(65535, round(v.maf() * 100)))
            return f"41 10 {(raw >> 8) & 0xFF:02X} {raw & 0xFF:02X}"

        if pid == '11':   # Throttle position
            val = max(0, min(255, round(v.throttle() * 2.55)))
            return f"41 11 {val:02X}"

        if pid == '13':   # O2 sensors present
            return "41 13 03"

        if pid == '14':   # O2 B1S1
            mv = round(v.o2_voltage_mv() / 5)
            return f"41 14 {mv:02X} FF"

        if pid == '15':   # O2 B1S2
            return "41 15 80 FF"

        if pid == '1C':   # OBD standard
            return "41 1C 01"

        if pid == '1F':   # Run time since engine start
            rt = min(65535, v.run_time_sec())
            return f"41 1F {(rt >> 8):02X} {rt & 0xFF:02X}"

        if pid == '20':
            return "41 20 80 07 E0 11"

        if pid == '2F':   # Fuel level
            return "41 2F 64"

        if pid == '33':   # Barometric pressure
            return "41 33 65"

        if pid == '40':
            return "41 40 FA DC 80 00"

        if pid == '42':   # Control module voltage
            val = round(v.voltage() * 1000)
            return f"41 42 {(val >> 8) & 0xFF:02X} {val & 0xFF:02X}"

        if pid == '46':   # Ambient air temp
            val = round(18.0 + 40)
            return f"41 46 {val:02X}"

        if pid == '4D':   # Time run with MIL
            elapsed_min = min(65535, v.run_time_sec() // 60)
            return f"41 4D {(elapsed_min >> 8):02X} {elapsed_min & 0xFF:02X}"

        if pid == '5C':   # Engine oil temp
            val = max(0, min(255, round(v.coolant_temp() - 8) + 40))
            return f"41 5C {val:02X}"

        if pid == '5E':   # Fuel rate (L/h * 20)
            rate = round((v.rpm() * 0.003 + 0.5) * 20)
            return f"41 5E {(rate >> 8) & 0xFF:02X} {rate & 0xFF:02X}"

        return "NODATA"

    # ------------------------------------------------------------------ #
    #  Mode 03 — DTC                                                       #
    # ------------------------------------------------------------------ #

    def _m03(self):
        dtcs = self.vehicle.get_dtcs()
        if not dtcs:
            return "43 00"
        count = len(dtcs)
        # dtcs — это список кортежей (raw_hex, code_str, description)
        bytes_str = " ".join(
            f"{int(raw[:2], 16):02X} {int(raw[2:], 16):02X}"
            for raw, _, _ in dtcs
        )
        return f"43 {count:02X} {bytes_str}"

    # ------------------------------------------------------------------ #
    #  Mode 09 — Vehicle Info                                              #
    # ------------------------------------------------------------------ #

    def _m09(self, pid):
        v = self.vehicle
        if pid == '00':
            return "49 00 55 40 00 00"
        if pid == '01':
            return "49 01 01"
        if pid == '02':   # VIN
            hex_vin = " ".join(f"{ord(c):02X}" for c in v.VIN)
            return f"49 02 01 00 00 00 {hex_vin}"
        if pid == '04':   # Calibration ID
            cal = "45 43 55 5F 43 41 4C 5F 32 2E 30 00 00 00 00 00"
            return f"49 04 01 {cal}"
        if pid == '0A':   # ECU name
            name = "45 43 55 53 49 4D 5F 32 2E 30 00 00 00 00 00 00"
            return f"49 0A 01 {name}"
        return "NODATA"

    # ------------------------------------------------------------------ #
    #  KWP2000                                                             #
    # ------------------------------------------------------------------ #

    def _kwp_dtc(self):
        dtcs = self.vehicle.get_dtcs()
        if not dtcs:
            return "53 00"
        count = len(dtcs)
        result = f"53 {count:02X}"
        for raw, _, _ in dtcs:
            b1 = int(raw[:2], 16)
            b2 = int(raw[2:], 16)
            result += f" {b1:02X} {b2:02X} 08"
        return result

    def _kwp_read(self, param_id):
        v = self.vehicle
        kwp = {
            '0101': lambda: f"61 01 01 {(round(v.rpm() * 4) >> 8) & 0xFF:02X} {round(v.rpm() * 4) & 0xFF:02X}",
            '0102': lambda: f"61 01 02 {round(v.speed()):02X}",
            '0103': lambda: f"61 01 03 {round(v.coolant_temp()) + 40:02X}",
            '0104': lambda: f"61 01 04 {round(v.intake_temp()) + 40:02X}",
            '0105': lambda: f"61 01 05 {round(v.throttle() * 2.55):02X}",
        }
        fn = kwp.get(param_id.upper())
        return fn() if fn else f"61 {param_id[:2]} {param_id[2:4]} 00"

    # ------------------------------------------------------------------ #
    #  UDS (ISO 14229)                                                     #
    # ------------------------------------------------------------------ #

    def _uds_dtc(self, sub):
        dtcs = self.vehicle.get_dtcs()
        if sub == '01':
            return f"59 01 FF {self.vehicle.dtc_count():02X}"
        if sub in ('02', '09', '0A'):
            if not dtcs:
                return "59 02 FF"
            result = "59 02 FF"
            for raw, _, _ in dtcs:
                b1 = int(raw[:2], 16)
                b2 = int(raw[2:], 16)
                result += f" {b1:02X} {b2:02X} 00 2F"
            return result
        return "7F 19 31"

    def _uds_read(self, did):
        v = self.vehicle
        did = did.upper()
        uds_map = {
            'F180': lambda: f"62 F1 80 56 32 2E 30 2E 30",
            'F181': lambda: f"62 F1 81 56 32 2E 30 2E 30",
            'F18C': lambda: f"62 F1 8C 45 43 55 53 49 4D 30 32",
            'F190': lambda: f"62 F1 90 " + " ".join(f"{ord(c):02X}" for c in v.VIN),
            'F193': lambda: f"62 F1 93 45 43 55 5F 48 57 5F 32 2E 30",
            'F194': lambda: f"62 F1 94 45 43 55 5F 53 57 5F 32 2E 30",
            'F1A0': lambda: f"62 F1 A0 41 50 50 5F 32 2E 30",
            'F200': lambda: (lambda r=round(v.rpm() * 4): f"62 F2 00 {(r>>8)&0xFF:02X} {r&0xFF:02X}")(),
            'F201': lambda: f"62 F2 01 {round(v.speed()):02X}",
            'F202': lambda: f"62 F2 02 {round(v.coolant_temp()) + 40:02X}",
            'F203': lambda: "62 F2 03 64",
            'F204': lambda: f"62 F2 04 {round(v.throttle() * 2.55):02X}",
            'F205': lambda: f"62 F2 05 {round(v.intake_temp()) + 40:02X}",
            'F206': lambda: f"62 F2 06 {round(v.fuel_pressure() / 3):02X}",
            'F208': lambda: "62 F2 08 A0",
        }
        fn = uds_map.get(did)
        return fn() if fn else f"7F 22 31"

    # ------------------------------------------------------------------ #
    #  Сервер                                                              #
    # ------------------------------------------------------------------ #

    def start(self):
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind(('0.0.0.0', self.port))
            self.server_socket.listen(5)
            self.server_socket.settimeout(1.0)

            v = self.vehicle
            print("=" * 60)
            print(f"  ELM327 Simulator v2.0  |  TCP:{self.port}")
            print("=" * 60)
            print(f"  VIN    : {v.VIN}")
            print(f"  DTC    : {[d[1] for d in v.ACTIVE_DTCS]}")
            print(f"  Режимы : OBD-II, KWP2000, UDS, CAN")
            print("=" * 60)
            print("  Нажмите Ctrl+C для остановки")

            while self.running:
                try:
                    client, address = self.server_socket.accept()
                    t = threading.Thread(target=self.handle_client, args=(client, address))
                    t.daemon = True
                    t.start()
                    self.client_threads.append(t)
                    self.client_threads = [x for x in self.client_threads if x.is_alive()]
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"Ошибка accept: {e}")
        except Exception as e:
            print(f"Ошибка запуска: {e}")
        finally:
            self.stop()

    def stop(self):
        print("\nОстановка симулятора...")
        self.running = False
        self.vehicle.engine_on = False
        for c in self.connected_clients[:]:
            try:
                c.close()
            except Exception:
                pass
        if self.server_socket:
            try:
                self.server_socket.close()
            except Exception:
                pass
        for t in self.client_threads:
            try:
                t.join(timeout=1)
            except Exception:
                pass
        print("Симулятор остановлен")


if __name__ == "__main__":
    sim = ELM327Simulator()
    try:
        sim.start()
    except KeyboardInterrupt:
        print("\nCtrl+C — останавливаем...")
        sim.stop()
