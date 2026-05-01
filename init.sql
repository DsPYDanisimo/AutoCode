-- init.sql для PostgreSQL

-- Создание таблицы DTC кодов
CREATE TABLE IF NOT EXISTS dtc_codes (
    id SERIAL PRIMARY KEY,
    code VARCHAR(10) NOT NULL UNIQUE,
    description TEXT NOT NULL,
    system_type VARCHAR(50),
    possible_causes TEXT[],
    symptoms TEXT[],
    severity INTEGER DEFAULT 3,
    manufacturer VARCHAR(100),
    model_range TEXT[],
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Создание таблицы истории ошибок
CREATE TABLE IF NOT EXISTS dtc_history (
    id SERIAL PRIMARY KEY,
    dtc_code VARCHAR(10) REFERENCES dtc_codes(code) ON DELETE CASCADE,
    vehicle_vin VARCHAR(17),
    read_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    mileage INTEGER,
    environment_data JSONB,
    status VARCHAR(20) DEFAULT 'active',
    cleared_time TIMESTAMP
);

-- Создание таблицы информации об ЭБУ
CREATE TABLE IF NOT EXISTS ecu_info (
    id SERIAL PRIMARY KEY,
    vin VARCHAR(17) UNIQUE,
    ecu_type VARCHAR(100),
    protocol VARCHAR(50),
    software_version VARCHAR(50),
    hardware_version VARCHAR(50),
    manufacturer VARCHAR(100),
    model VARCHAR(100),
    year INTEGER,
    last_connection TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Создание таблицы live данных
CREATE TABLE IF NOT EXISTS live_data_log (
    id SERIAL PRIMARY KEY,
    vin VARCHAR(17) REFERENCES ecu_info(vin) ON DELETE CASCADE,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    rpm INTEGER,
    speed INTEGER,
    coolant_temp INTEGER,
    throttle_position REAL,
    fuel_pressure REAL,
    intake_temp INTEGER,
    maf_rate REAL,
    voltage REAL,
    data JSONB
);

-- Создание таблицы калибровок
CREATE TABLE IF NOT EXISTS calibrations (
    id SERIAL PRIMARY KEY,
    name VARCHAR(200) NOT NULL,
    description TEXT,
    vehicle_make VARCHAR(100),
    vehicle_model VARCHAR(100),
    vehicle_year INTEGER,
    engine_code VARCHAR(50),
    ecu_type VARCHAR(100),
    parameters JSONB NOT NULL,
    author VARCHAR(100),
    version VARCHAR(20),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    is_verified BOOLEAN DEFAULT FALSE,
    rating INTEGER DEFAULT 0
);

-- Создание индексов для оптимизации
CREATE INDEX IF NOT EXISTS idx_dtc_history_vin ON dtc_history(vehicle_vin);
CREATE INDEX IF NOT EXISTS idx_dtc_history_time ON dtc_history(read_time);
CREATE INDEX IF NOT EXISTS idx_live_data_vin ON live_data_log(vin);
CREATE INDEX IF NOT EXISTS idx_live_data_timestamp ON live_data_log(timestamp);
CREATE INDEX IF NOT EXISTS idx_calibrations_ecu ON calibrations(ecu_type);

-- Добавление базовых DTC кодов (если таблица пуста)
INSERT INTO dtc_codes (code, description, system_type, severity)
SELECT * FROM (VALUES
    ('P0100', 'Mass or Volume Air Flow Circuit Malfunction', 'Powertrain', 3),
    ('P0101', 'Mass or Volume Air Flow Circuit Range/Performance', 'Powertrain', 3),
    ('P0102', 'Mass or Volume Air Flow Circuit Low Input', 'Powertrain', 4),
    ('P0103', 'Mass or Volume Air Flow Circuit High Input', 'Powertrain', 4),
    ('P0110', 'Intake Air Temperature Circuit Malfunction', 'Powertrain', 2),
    ('P0115', 'Engine Coolant Temperature Circuit Malfunction', 'Powertrain', 3),
    ('P0120', 'Throttle/Pedal Position Sensor Circuit Malfunction', 'Powertrain', 4),
    ('P0130', 'O2 Sensor Circuit Malfunction', 'Powertrain', 3),
    ('P0171', 'System Too Lean', 'Powertrain', 3),
    ('P0172', 'System Too Rich', 'Powertrain', 3),
    ('P0300', 'Random/Multiple Cylinder Misfire Detected', 'Powertrain', 4),
    ('P0420', 'Catalyst System Efficiency Below Threshold', 'Powertrain', 3),
    ('P0440', 'Evaporative Emission Control System Malfunction', 'Powertrain', 1),
    ('P0500', 'Vehicle Speed Sensor Malfunction', 'Powertrain', 2),
    ('P0560', 'System Voltage Malfunction', 'Powertrain', 2),
    ('P0600', 'Serial Communication Link Malfunction', 'Powertrain', 4),
    ('P0606', 'ECU Processor Fault', 'Powertrain', 5),
    ('P0700', 'Transmission Control System Malfunction', 'Powertrain', 3),
    ('U0100', 'Lost Communication With ECU/PCM', 'Network', 4),
    ('U0121', 'Lost Communication With ABS Module', 'Network', 3),
    ('C0035', 'Left Front Wheel Speed Sensor', 'Chassis', 3),
    ('B1000', 'ECU Malfunction', 'Body', 4)
) AS new_codes(code, description, system_type, severity)
WHERE NOT EXISTS (SELECT 1 FROM dtc_codes WHERE code = new_codes.code);