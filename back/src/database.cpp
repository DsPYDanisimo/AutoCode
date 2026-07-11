#include "../include/database.h"
#include "../include/json.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <functional>

using json = nlohmann::json;

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CHECK_MARK "[OK]"
#define CROSS_MARK "[ERROR]"

// Коллбэк для подсчета записей
int Database::callback_select_count(void* data, int argc, char** argv, char** azColName) {
    int* count = static_cast<int*>(data);
    if (argc > 0 && argv[0] && argv[0][0] != '\0') {
        try { *count = std::stoi(argv[0]); } catch (...) {}
    }
    return 0;
}

// Коллбэк для DTC записей
int Database::callback_select_dtc(void* data, int argc, char** argv, char** azColName) {
    std::vector<DTCRecord>* records = static_cast<std::vector<DTCRecord>*>(data);

    DTCRecord dtc;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        std::string value = argv[i] ? argv[i] : "";

        if (colName == "code") dtc.code = value;
        else if (colName == "description") dtc.description = value;
        else if (colName == "system_type") dtc.system_type = value;
        else if (colName == "possible_causes") {
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) dtc.possible_causes.push_back(item);
            }
        }
        else if (colName == "symptoms") {
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) dtc.symptoms.push_back(item);
            }
        }
        else if (colName == "severity") { try { dtc.severity = std::stoi(value); } catch (...) {} }
    }

    records->push_back(dtc);
    return 0;
}

// Коллбэк для всех DTC записей
int Database::callback_select_all_dtc(void* data, int argc, char** argv, char** azColName) {
    json* j = static_cast<json*>(data);

    json dtc_json;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        std::string value = argv[i] ? argv[i] : "";

        if (colName == "code") dtc_json["code"] = value;
        else if (colName == "description") dtc_json["description"] = value;
        else if (colName == "system_type") dtc_json["system_type"] = value;
        else if (colName == "possible_causes") {
            std::vector<std::string> causes;
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) causes.push_back(item);
            }
            dtc_json["possible_causes"] = causes;
        }
        else if (colName == "symptoms") {
            std::vector<std::string> symptoms;
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) symptoms.push_back(item);
            }
            dtc_json["symptoms"] = symptoms;
        }
        else if (colName == "severity") { try { dtc_json["severity"] = std::stoi(value); } catch (...) { dtc_json["severity"] = 3; } }
    }

    j->push_back(dtc_json);
    return 0;
}

// Коллбэк для истории
int Database::callback_select_history(void* data, int argc, char** argv, char** azColName) {
    std::vector<DTCInstance>* records = static_cast<std::vector<DTCInstance>*>(data);

    DTCInstance instance;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        std::string value = argv[i] ? argv[i] : "";

        if (colName == "dtc_code") instance.dtc_code = value;
        else if (colName == "description") instance.description = value;
        else if (colName == "read_time") instance.read_time = value;
        else if (colName == "mileage") { try { instance.mileage = std::stoi(value); } catch (...) { instance.mileage = 0; } }
        else if (colName == "environment_data") instance.environment_data = value;
        else if (colName == "status") instance.status = value;
    }

    records->push_back(instance);
    return 0;
}

// Коллбэк для ECU info
int Database::callback_select_ecu(void* data, int argc, char** argv, char** azColName) {
    ECUInfo* info = static_cast<ECUInfo*>(data);

    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        std::string value = argv[i] ? argv[i] : "";

        if (colName == "vin") info->vin = value;
        else if (colName == "ecu_type") info->ecu_type = value;
        else if (colName == "protocol") info->protocol = value;
        else if (colName == "software_version") info->software_version = value;
        else if (colName == "hardware_version") info->hardware_version = value;
        else if (colName == "manufacturer") info->manufacturer = value;
        else if (colName == "model") info->model = value;
        else if (colName == "year") { try { info->year = std::stoi(value); } catch (...) { info->year = 0; } }
        else if (colName == "last_connection") info->last_connection = value;
    }

    return 0;
}

// Коллбэк для live данных
int Database::callback_select_live(void* data, int argc, char** argv, char** azColName) {
    std::vector<LiveData>* records = static_cast<std::vector<LiveData>*>(data);

    LiveData ld;
    for (int i = 0; i < argc; i++) {
        std::string colName = azColName[i];
        std::string value = argv[i] ? argv[i] : "0";

        if (colName == "timestamp") ld.timestamp = value;
        else if (colName == "rpm") { try { ld.rpm = std::stoi(value); } catch (...) {} }
        else if (colName == "speed") { try { ld.speed = std::stoi(value); } catch (...) {} }
        else if (colName == "coolant_temp") { try { ld.coolant_temp = std::stoi(value); } catch (...) {} }
        else if (colName == "throttle_position") { try { ld.throttle_position = std::stof(value); } catch (...) {} }
        else if (colName == "fuel_pressure") { try { ld.fuel_pressure = std::stof(value); } catch (...) {} }
        else if (colName == "intake_temp") { try { ld.intake_temp = std::stoi(value); } catch (...) {} }
        else if (colName == "maf_rate") { try { ld.maf_rate = std::stof(value); } catch (...) {} }
        else if (colName == "voltage") { try { ld.voltage = std::stof(value); } catch (...) {} }
        else if (colName == "data") ld.raw_data = value;
    }

    records->push_back(ld);
    return 0;
}

Database::Database() : db_(nullptr), is_connected_(false) {
    std::cout << "Инициализация базы данных SQLite..." << std::endl;
}

Database::~Database() {
    disconnect();
}

bool Database::execute_sql(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        last_error_ = errMsg ? errMsg : "Unknown SQLite error";
        std::cerr << CROSS_MARK << " SQL Error: " << last_error_ << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::connect(const std::string& db_path) {
    db_path_ = db_path;

    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << CROSS_MARK << " Не удалось открыть базу данных: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_close(db_);
        db_ = nullptr;
        is_connected_ = false;
        return false;
    }

    is_connected_ = true;
    std::cout << CHECK_MARK << " Подключено к SQLite базе: " << db_path_ << std::endl;

    // Включаем поддержку внешних ключей
    execute_sql("PRAGMA foreign_keys = ON;");

    // Создаем таблицы, если их нет
    create_tables_if_not_exist();

    return true;
}

void Database::disconnect() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        is_connected_ = false;
        std::cout << "Отключено от базы данных" << std::endl;
    }
}

void Database::create_tables_if_not_exist() {
    if (!db_) return;

    // Таблица для DTC кодов
    std::string sql_dtc = R"(
        CREATE TABLE IF NOT EXISTS dtc_codes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            code VARCHAR(10) NOT NULL UNIQUE,
            description TEXT NOT NULL,
            system_type VARCHAR(50),
            possible_causes TEXT,
            symptoms TEXT,
            severity INTEGER DEFAULT 3,
            manufacturer VARCHAR(100),
            model_range TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    )";

    // Таблица для истории ошибок
    std::string sql_history = R"(
        CREATE TABLE IF NOT EXISTS dtc_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            dtc_code VARCHAR(10),
            vehicle_vin VARCHAR(17),
            read_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            mileage INTEGER,
            environment_data TEXT,
            status VARCHAR(20) DEFAULT 'active',
            cleared_time TIMESTAMP,
            FOREIGN KEY (dtc_code) REFERENCES dtc_codes(code) ON DELETE CASCADE
        )
    )";

    // Таблица для информации об ЭБУ
    std::string sql_ecu = R"(
        CREATE TABLE IF NOT EXISTS ecu_info (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vin VARCHAR(17) UNIQUE,
            ecu_type VARCHAR(100),
            protocol VARCHAR(50),
            software_version VARCHAR(50),
            hardware_version VARCHAR(50),
            manufacturer VARCHAR(100),
            model VARCHAR(100),
            year INTEGER,
            last_connection TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    )";

    // Таблица для live данных
    std::string sql_live = R"(
        CREATE TABLE IF NOT EXISTS live_data_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vin VARCHAR(17),
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            rpm INTEGER,
            speed INTEGER,
            coolant_temp INTEGER,
            throttle_position REAL,
            fuel_pressure REAL,
            intake_temp INTEGER,
            maf_rate REAL,
            voltage REAL,
            data TEXT,
            FOREIGN KEY (vin) REFERENCES ecu_info(vin) ON DELETE CASCADE
        )
    )";

    // Таблица для калибровок
    std::string sql_calib = R"(
        CREATE TABLE IF NOT EXISTS calibrations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name VARCHAR(200) NOT NULL,
            description TEXT,
            vehicle_make VARCHAR(100),
            vehicle_model VARCHAR(100),
            vehicle_year INTEGER,
            engine_code VARCHAR(50),
            ecu_type VARCHAR(100),
            parameters TEXT NOT NULL,
            author VARCHAR(100),
            version VARCHAR(20),
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            is_verified INTEGER DEFAULT 0,
            rating INTEGER DEFAULT 0
        )
    )";

    // Создаем индексы
    std::string idx_history_vin = "CREATE INDEX IF NOT EXISTS idx_dtc_history_vin ON dtc_history(vehicle_vin)";
    std::string idx_history_time = "CREATE INDEX IF NOT EXISTS idx_dtc_history_time ON dtc_history(read_time)";
    std::string idx_live_vin = "CREATE INDEX IF NOT EXISTS idx_live_data_vin ON live_data_log(vin)";
    std::string idx_live_time = "CREATE INDEX IF NOT EXISTS idx_live_data_timestamp ON live_data_log(timestamp)";
    std::string idx_calib_ecu = "CREATE INDEX IF NOT EXISTS idx_calibrations_ecu ON calibrations(ecu_type)";

    execute_sql(sql_dtc);
    execute_sql(sql_history);
    execute_sql(sql_ecu);
    execute_sql(sql_live);
    execute_sql(sql_calib);
    execute_sql(idx_history_vin);
    execute_sql(idx_history_time);
    execute_sql(idx_live_vin);
    execute_sql(idx_live_time);
    execute_sql(idx_calib_ecu);

    std::cout << "Таблицы базы данных проверены/созданы" << std::endl;

    // Заполняем базовыми DTC кодами, если таблица пуста
    if (is_dtc_table_empty()) {
        populate_default_dtc_codes();
        initialize_extended_dtc_database();     // Добавляем расширенные коды
        initialize_manufacturer_dtc_database();  // Добавляем коды VAG/BMW/Mercedes
    }
}

bool Database::is_dtc_table_empty() {
    if (!db_) return true;

    std::string sql = "SELECT COUNT(*) FROM dtc_codes";
    int count = 0;

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), callback_select_count, &count, &errMsg);

    if (rc != SQLITE_OK) {
        std::cerr << "Ошибка проверки таблицы dtc_codes: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return true;
    }

    return count == 0;
}

int Database::get_dtc_count() {
    if (!db_) return 0;

    std::string sql = "SELECT COUNT(*) FROM dtc_codes";
    int count = 0;

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), callback_select_count, &count, &errMsg);

    if (rc != SQLITE_OK) {
        std::cerr << "Ошибка подсчета DTC: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return 0;
    }

    return count;
}

bool Database::add_dtc_code(const DTCRecord& dtc) {
    if (!db_) return false;

    // Преобразуем векторы в строки с разделителем |
    std::string causes_str;
    for (size_t i = 0; i < dtc.possible_causes.size(); ++i) {
        if (i > 0) causes_str += "|";
        causes_str += dtc.possible_causes[i];
    }

    std::string symptoms_str;
    for (size_t i = 0; i < dtc.symptoms.size(); ++i) {
        if (i > 0) symptoms_str += "|";
        symptoms_str += dtc.symptoms[i];
    }

    std::string sql = R"(
        INSERT OR REPLACE INTO dtc_codes 
        (code, description, system_type, possible_causes, symptoms, severity, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, dtc.code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, dtc.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, dtc.system_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, causes_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, symptoms_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, dtc.severity);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    return true;
}

bool Database::add_dtc_codes_batch(const std::vector<DTCRecord>& dtc_list) {
    if (!db_) return false;

    bool success = true;
    for (const auto& dtc : dtc_list) {
        if (!add_dtc_code(dtc)) {
            success = false;
            std::cerr << "Ошибка добавления DTC: " << dtc.code << std::endl;
        }
    }

    return success;
}

bool Database::delete_dtc_code(const std::string& code) {
    if (!db_) return false;

    std::string sql = "DELETE FROM dtc_codes WHERE code = ?";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool Database::update_dtc_code(const DTCRecord& dtc) {
    if (!db_) return false;

    std::string causes_str;
    for (size_t i = 0; i < dtc.possible_causes.size(); ++i) {
        if (i > 0) causes_str += "|";
        causes_str += dtc.possible_causes[i];
    }

    std::string symptoms_str;
    for (size_t i = 0; i < dtc.symptoms.size(); ++i) {
        if (i > 0) symptoms_str += "|";
        symptoms_str += dtc.symptoms[i];
    }

    std::string sql = R"(
        UPDATE dtc_codes 
        SET description = ?, system_type = ?, possible_causes = ?, 
            symptoms = ?, severity = ?, updated_at = CURRENT_TIMESTAMP
        WHERE code = ?
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, dtc.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, dtc.system_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, causes_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, symptoms_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, dtc.severity);
    sqlite3_bind_text(stmt, 6, dtc.code.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::optional<DTCRecord> Database::get_dtc_by_code(const std::string& code) {
    if (!db_) return std::nullopt;

    std::string sql = "SELECT code, description, system_type, possible_causes, symptoms, severity FROM dtc_codes WHERE code = ?";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_STATIC);

    std::vector<DTCRecord> records;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DTCRecord dtc;
        const char* col0 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* col1 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* col2 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        dtc.code = col0 ? col0 : "";
        dtc.description = col1 ? col1 : "";
        dtc.system_type = col2 ? col2 : "";

        const char* causes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (causes) {
            std::stringstream ss(causes);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) dtc.possible_causes.push_back(item);
            }
        }

        const char* symptoms = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (symptoms) {
            std::stringstream ss(symptoms);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) dtc.symptoms.push_back(item);
            }
        }

        dtc.severity = sqlite3_column_int(stmt, 5);
        records.push_back(dtc);
    }

    sqlite3_finalize(stmt);

    if (!records.empty()) {
        return records[0];
    }

    return std::nullopt;
}

std::vector<DTCRecord> Database::search_dtc_codes(const std::string& query) {
    std::vector<DTCRecord> results;
    if (!db_) return results;

    std::string sql = R"(
        SELECT code, description, system_type, possible_causes, symptoms, severity 
        FROM dtc_codes 
        WHERE code LIKE ? OR description LIKE ? 
        ORDER BY 
            CASE 
                WHEN code LIKE ? THEN 1
                WHEN description LIKE ? THEN 2
                ELSE 3
            END, code
        LIMIT 100
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return results;
    }

    std::string query_pattern = "%" + query + "%";
    std::string query_start = query + "%";

    sqlite3_bind_text(stmt, 1, query_pattern.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, query_pattern.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, query_start.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, query_start.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DTCRecord dtc;
        const char* col0 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* col1 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* col2 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        dtc.code = col0 ? col0 : "";
        dtc.description = col1 ? col1 : "";
        dtc.system_type = col2 ? col2 : "";

        const char* causes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (causes) {
            std::stringstream ss(causes);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) dtc.possible_causes.push_back(item);
            }
        }

        const char* symptoms = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (symptoms) {
            std::stringstream ss(symptoms);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) dtc.symptoms.push_back(item);
            }
        }

        dtc.severity = sqlite3_column_int(stmt, 5);
        results.push_back(dtc);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<DTCRecord> Database::get_all_dtc_codes() {
    std::vector<DTCRecord> results;
    if (!db_) return results;

    std::string sql = "SELECT code, description, system_type, possible_causes, symptoms, severity FROM dtc_codes ORDER BY code";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DTCRecord dtc;
        const char* col0 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* col1 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* col2 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        dtc.code = col0 ? col0 : "";
        dtc.description = col1 ? col1 : "";
        dtc.system_type = col2 ? col2 : "";

        const char* causes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (causes) {
            std::stringstream ss(causes);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) dtc.possible_causes.push_back(item);
            }
        }

        const char* symptoms = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (symptoms) {
            std::stringstream ss(symptoms);
            std::string item;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) dtc.symptoms.push_back(item);
            }
        }

        dtc.severity = sqlite3_column_int(stmt, 5);
        results.push_back(dtc);
    }

    sqlite3_finalize(stmt);
    return results;
}

bool Database::add_to_history(const std::string& dtc_code, const std::string& vin,
    int mileage, const std::string& environment_data) {
    if (!db_) return false;

    std::string check_sql = "SELECT id FROM dtc_history WHERE dtc_code = ? AND vehicle_vin = ? AND status = 'active' LIMIT 1";

    sqlite3_stmt* check_stmt;
    int rc = sqlite3_prepare_v2(db_, check_sql.c_str(), -1, &check_stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(check_stmt, 1, dtc_code.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(check_stmt, 2, vin.c_str(), -1, SQLITE_STATIC);

    bool exists = (sqlite3_step(check_stmt) == SQLITE_ROW);
    sqlite3_finalize(check_stmt);

    if (!exists) {
        std::string insert_sql = R"(
            INSERT INTO dtc_history (dtc_code, vehicle_vin, mileage, environment_data)
            VALUES (?, ?, ?, ?)
        )";

        sqlite3_stmt* insert_stmt;
        rc = sqlite3_prepare_v2(db_, insert_sql.c_str(), -1, &insert_stmt, nullptr);
        if (rc != SQLITE_OK) {
            last_error_ = sqlite3_errmsg(db_);
            return false;
        }

        sqlite3_bind_text(insert_stmt, 1, dtc_code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 2, vin.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(insert_stmt, 3, mileage);
        sqlite3_bind_text(insert_stmt, 4, environment_data.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(insert_stmt);
        sqlite3_finalize(insert_stmt);

        if (rc != SQLITE_DONE) {
            last_error_ = sqlite3_errmsg(db_);
            return false;
        }
    }

    return true;
}

std::vector<DTCInstance> Database::get_history(const std::string& vin, int limit) {
    std::vector<DTCInstance> history;
    if (!db_) return history;

    std::string sql = R"(
        SELECT h.dtc_code, h.read_time, h.mileage, h.environment_data, h.status, 
               d.description
        FROM dtc_history h
        LEFT JOIN dtc_codes d ON h.dtc_code = d.code
        WHERE h.vehicle_vin = ?
        ORDER BY h.read_time DESC
        LIMIT ?
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return history;
    }

    sqlite3_bind_text(stmt, 1, vin.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DTCInstance instance;
        instance.dtc_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        instance.read_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        instance.mileage = sqlite3_column_int(stmt, 2);

        const char* env_data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        instance.environment_data = env_data ? env_data : "";

        instance.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

        const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        instance.description = desc ? desc : "Unknown DTC";

        history.push_back(instance);
    }

    sqlite3_finalize(stmt);
    return history;
}

bool Database::clear_history(const std::string& vin) {
    if (!db_) return false;

    std::string sql;
    if (vin.empty()) {
        sql = "DELETE FROM dtc_history";
    }
    else {
        sql = "DELETE FROM dtc_history WHERE vehicle_vin = ?";
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    if (!vin.empty()) {
        sqlite3_bind_text(stmt, 1, vin.c_str(), -1, SQLITE_STATIC);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<std::string> Database::get_all_vin() {
    std::vector<std::string> vins;
    if (!db_) return vins;

    std::string sql = "SELECT vin FROM ecu_info ORDER BY last_connection DESC";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return vins;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* vin = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (vin) {
            vins.push_back(vin);
        }
    }

    sqlite3_finalize(stmt);
    return vins;
}

bool Database::update_ecu_info(const std::string& vin, const ECUInfo& info) {
    if (!db_) return false;

    std::string sql = R"(
        INSERT OR REPLACE INTO ecu_info 
        (vin, ecu_type, protocol, software_version, hardware_version, manufacturer, model, year, last_connection)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, vin.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, info.ecu_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, info.protocol.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, info.software_version.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, info.hardware_version.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, info.manufacturer.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, info.model.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, info.year);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    return true;
}

std::optional<ECUInfo> Database::get_ecu_info(const std::string& vin) {
    if (!db_) return std::nullopt;

    std::string sql = "SELECT vin, ecu_type, protocol, software_version, hardware_version, manufacturer, model, year, last_connection FROM ecu_info WHERE vin = ?";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, vin.c_str(), -1, SQLITE_STATIC);

    ECUInfo info;
    bool found = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        info.vin = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        info.ecu_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.protocol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.software_version = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        info.hardware_version = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        info.manufacturer = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        info.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        info.year = sqlite3_column_int(stmt, 7);
        info.last_connection = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    }

    sqlite3_finalize(stmt);

    if (found) {
        return info;
    }

    return std::nullopt;
}

bool Database::log_live_data(const std::string& vin, const LiveData& data) {
    if (!db_) return false;

    std::string sql = R"(
        INSERT INTO live_data_log 
        (vin, rpm, speed, coolant_temp, throttle_position, fuel_pressure, intake_temp, maf_rate, voltage, data)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, vin.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, data.rpm);
    sqlite3_bind_int(stmt, 3, data.speed);
    sqlite3_bind_int(stmt, 4, data.coolant_temp);
    sqlite3_bind_double(stmt, 5, data.throttle_position);
    sqlite3_bind_double(stmt, 6, data.fuel_pressure);
    sqlite3_bind_int(stmt, 7, data.intake_temp);
    sqlite3_bind_double(stmt, 8, data.maf_rate);
    sqlite3_bind_double(stmt, 9, data.voltage);
    sqlite3_bind_text(stmt, 10, data.raw_data.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    return true;
}

std::vector<LiveData> Database::get_live_data_history(const std::string& vin,
    const std::string& from_time,
    const std::string& to_time) {
    std::vector<LiveData> history;
    if (!db_) return history;

    std::string sql = R"(
        SELECT timestamp, rpm, speed, coolant_temp, throttle_position, 
               fuel_pressure, intake_temp, maf_rate, voltage, data 
        FROM live_data_log 
        WHERE vin = ? AND datetime(timestamp) BETWEEN datetime(?) AND datetime(?)
        ORDER BY timestamp DESC
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return history;
    }

    sqlite3_bind_text(stmt, 1, vin.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, from_time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, to_time.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LiveData data;
        data.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        data.rpm = sqlite3_column_int(stmt, 1);
        data.speed = sqlite3_column_int(stmt, 2);
        data.coolant_temp = sqlite3_column_int(stmt, 3);
        data.throttle_position = static_cast<float>(sqlite3_column_double(stmt, 4));
        data.fuel_pressure = static_cast<float>(sqlite3_column_double(stmt, 5));
        data.intake_temp = sqlite3_column_int(stmt, 6);
        data.maf_rate = static_cast<float>(sqlite3_column_double(stmt, 7));
        data.voltage = static_cast<float>(sqlite3_column_double(stmt, 8));

        const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        data.raw_data = raw ? raw : "";

        history.push_back(data);
    }

    sqlite3_finalize(stmt);
    return history;
}

bool Database::cleanup_old_records(int days_to_keep) {
    if (!db_) return false;

    char* errMsg = nullptr;

    std::string sql_history = "DELETE FROM dtc_history WHERE julianday('now') - julianday(read_time) > " +
        std::to_string(days_to_keep);

    std::string sql_live = "DELETE FROM live_data_log WHERE julianday('now') - julianday(timestamp) > " +
        std::to_string(days_to_keep);

    int rc1 = sqlite3_exec(db_, sql_history.c_str(), nullptr, nullptr, &errMsg);
    if (rc1 != SQLITE_OK) {
        std::cerr << "Ошибка очистки истории: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }

    int rc2 = sqlite3_exec(db_, sql_live.c_str(), nullptr, nullptr, &errMsg);
    if (rc2 != SQLITE_OK) {
        std::cerr << "Ошибка очистки live данных: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }

    std::cout << "Очистка завершена" << std::endl;
    return true;
}

void Database::initialize_extended_dtc_database() {
    std::cout << "Добавление расширенных DTC кодов..." << std::endl;

    std::vector<DTCRecord> extended_dtcs = {
        {"P0010", "A Camshaft Position Actuator Circuit/Open (Bank 1)", "Powertrain",
         {"Faulty variable valve timing solenoid", "Wiring issue", "Low oil pressure"},
         {"Check engine light", "Rough idle", "Reduced performance"}, 3},

        {"P0011", "A Camshaft Position Timing Over-Advanced or System Performance (Bank 1)", "Powertrain",
         {"Sludge in engine", "Faulty VVT solenoid", "Timing chain stretched", "Low oil pressure"},
         {"Engine noise", "Poor fuel economy", "Check engine light"}, 3},

        {"P0012", "A Camshaft Position Timing Over-Retarded (Bank 1)", "Powertrain",
         {"Faulty VVT solenoid", "Timing chain jumped", "ECU issue"},
         {"Poor performance", "Check engine light"}, 3},

        {"P0013", "B Camshaft Position Actuator Circuit/Open (Bank 1)", "Powertrain",
         {"Faulty VVT solenoid", "Wiring issue", "ECU problem"},
         {"Check engine light", "Rough idle"}, 2},

        {"P0014", "B Camshaft Position Timing Over-Advanced or System Performance (Bank 1)", "Powertrain",
         {"Sludge", "Faulty solenoid", "Timing issue"},
         {"Poor performance", "Check engine light"}, 3},

        {"P0015", "B Camshaft Position Timing Over-Retarded (Bank 1)", "Powertrain",
         {"Faulty VVT", "Timing chain", "ECU issue"},
         {"Check engine light", "Rough running"}, 3},

        {"P0016", "Crankshaft Position Camshaft Position Correlation (Bank 1 Sensor A)", "Powertrain",
         {"Timing chain stretched", "Timing chain jumped", "Faulty sensors", "Worn guides"},
         {"Engine won't start", "Check engine light", "Engine noise"}, 4},

        {"P0017", "Crankshaft Position Camshaft Position Correlation (Bank 1 Sensor B)", "Powertrain",
         {"Timing chain stretched", "Timing chain jumped", "Faulty sensors"},
         {"Engine won't start", "Check engine light", "Engine noise"}, 4},

        {"P0018", "Crankshaft Position Camshaft Position Correlation (Bank 2 Sensor A)", "Powertrain",
         {"Timing chain stretched", "Timing chain jumped", "Faulty sensors"},
         {"Engine won't start", "Check engine light", "Engine noise"}, 4},

        {"P0019", "Crankshaft Position Camshaft Position Correlation (Bank 2 Sensor B)", "Powertrain",
         {"Timing chain stretched", "Timing chain jumped", "Faulty sensors"},
         {"Engine won't start", "Check engine light", "Engine noise"}, 4}
    };

    int success_count = 0;
    for (const auto& dtc : extended_dtcs) {
        if (add_dtc_code(dtc)) {
            success_count++;
        }
    }

    std::cout << "Добавлено расширенных DTC кодов: " << success_count
        << " из " << extended_dtcs.size() << std::endl;
}

void Database::initialize_manufacturer_dtc_database() {
    std::cout << "Добавление кодов производителей (VAG/BMW/Mercedes)..." << std::endl;

    std::vector<DTCRecord> manufacturer_dtcs = {
        // ── VAG: Volkswagen / Audi / Skoda / SEAT ───────────────────────────
        {"P1128", "[VAG] Boost pressure signal implausible/unstable (TSI/TDI)", "Turbocharger",
         {"Air leak after turbo", "Intercooler leak", "Faulty N75 diverter valve", "Clogged oil separator"},
         {"Reduced power", "Check engine light"}, 3},

        {"P1136", "[VAG] Long term fuel trim, bank 1: system too lean", "Fuel System",
         {"Stretched timing chain (EA888/EA111)", "Intake air leak", "Dirty injectors", "Low fuel pressure"},
         {"Rough idle", "Poor fuel economy"}, 3},

        {"P1279", "[VAG] Boost pressure sensor implausible signal at rest", "Turbocharger",
         {"Faulty boost pressure sensor", "Vacuum hose leak", "Clogged throttle body"},
         {"Check engine light"}, 2},

        {"P1336", "[VAG] Engine torque monitoring: adaptation at limit", "Timing",
         {"Stretched or jumped timing chain", "Incorrect valve timing", "Worn timing chain tensioner"},
         {"Engine noise", "Reduced power", "Check engine light"}, 4},

        {"P1550", "[VAG] Boost pressure sensor signal out of range/implausible", "Turbocharger",
         {"Open sensor circuit", "Sensor mechanically damaged", "Miscalibration after repair"},
         {"Check engine light", "Reduced power"}, 3},

        {"P1602", "[VAG] Power supply terminal 15 (ignition) malfunction", "Electrical",
         {"Poor ignition switch contact", "Wiring fault on terminal 15", "Weak battery"},
         {"Intermittent electrical faults"}, 2},

        {"P1611", "[VAG] TCM/DSG control module requests MIL (emissions-related fault)", "Transmission",
         {"DSG mechatronic fault", "DSG clutch adaptation error", "CAN communication loss with TCM"},
         {"Harsh shifting", "Check engine light"}, 3},

        {"P1614", "[VAG] Engine control module internal EEPROM fault", "Engine Control Module",
         {"ECU memory failure", "Improper tuning/chiptuning flash", "Voltage spike on board network"},
         {"Erratic engine behavior", "Check engine light"}, 3},

        {"P1621", "[VAG] Internal control module fault (limp-home/component protection)", "Engine Control Module",
         {"ECU overheating", "ECU hardware failure", "Improper software flash"},
         {"Limp mode", "Reduced power"}, 4},

        {"P1682", "[VAG] Low/unstable on-board supply voltage", "Electrical",
         {"Weak battery", "Poor engine ground connection", "Faulty alternator"},
         {"Intermittent electrical faults", "Check engine light"}, 2},

        // ── BMW ───────────────────────────────────────────────────────────
        {"P1055", "[BMW] VANOS intake solenoid, bank 1: implausible signal", "VANOS (Variable Timing)",
         {"Contaminated VANOS solenoid", "Low oil pressure", "Clogged VANOS oil passage"},
         {"Rough idle", "Reduced power"}, 3},

        {"P1093", "[BMW] Low fuel rail pressure, bank 1 (HPFP)", "Fuel System",
         {"Worn high-pressure fuel pump (N54/N55)", "Faulty fuel pressure regulator", "Clogged fuel filter"},
         {"Loss of power", "Engine stall", "Check engine light"}, 4},

        {"P1297", "[BMW] Possible large intake system leak (boost pressure implausible)", "Turbocharger",
         {"Cracked intercooler hose", "Loose intake connection", "Faulty boost pressure sensor"},
         {"Reduced power", "Check engine light"}, 3},

        {"P1443", "[BMW] EVAP purge valve: circuit malfunction", "EVAP System",
         {"Open wiring to purge valve", "Valve stuck", "Damaged charcoal canister"},
         {"Check engine light"}, 2},

        {"P1315", "[BMW] Misfire detected — catalyst damage risk", "Ignition System",
         {"Faulty ignition coils (N43/N53/N54)", "Worn spark plugs", "Vacuum leak"},
         {"Rough running", "Check engine light", "Catalyst damage"}, 4},

        {"P1626", "[BMW] Immobilizer synchronization fault (EWS/CAS)", "Immobilizer",
         {"ECU/key desynchronization", "Weak key battery", "ECU replaced without coding"},
         {"Engine won't start"}, 2},

        // ── Mercedes-Benz ────────────────────────────────────────────────
        {"P1088", "[Mercedes] Camshaft timing adjustment implausible, intake (M271/M272)", "Timing",
         {"Stretched timing chain", "Faulty camshaft adjustment solenoid", "Contaminated oil passage"},
         {"Rough idle", "Reduced power"}, 3},

        {"P1301", "[Mercedes] Multi-cylinder misfire — possible balance shaft damage (M271)", "Timing",
         {"Balance shaft gear failure", "Balance shaft chain failure", "Low oil pressure"},
         {"Severe engine noise", "Engine damage risk"}, 5},

        {"P1417", "[Mercedes] Secondary air injection pump malfunction", "Emission System",
         {"Seized secondary air pump", "Blown pump fuse", "Leaking valve"},
         {"Check engine light"}, 2},

        {"P1506", "[Mercedes] Accelerator pedal position sensor implausible signal", "Engine Control Module",
         {"Worn pedal potentiometer", "Wiring fault", "Poor connector contact"},
         {"Reduced power", "Check engine light"}, 3},

        {"P1590", "[Mercedes] Torque limited due to boost pressure sensor fault", "Turbocharger",
         {"Faulty boost pressure sensor", "Intake system leak", "Clogged air filter"},
         {"Reduced power"}, 3},

        {"P1622", "[Mercedes] SBC (electro-hydraulic brake) control unit malfunction", "Brakes (SBC)",
         {"Worn SBC hydraulic pump", "High-pressure brake fluid leak", "SBC unit failure (common on W211/R230)"},
         {"Brake warning light", "Reduced braking assist"}, 5},

        {"P1729", "[Mercedes] Air suspension (Airmatic) control valve malfunction", "Air Suspension",
         {"Air spring leak", "Suspension compressor failure", "Valve block leak"},
         {"Vehicle sits low", "Suspension warning light"}, 3},

        {"P1750", "[Mercedes] Transmission shift solenoid circuit malfunction (722.6/722.9)", "Transmission",
         {"Worn shift solenoid", "Contaminated valve body", "Low transmission fluid"},
         {"Harsh/delayed shifting"}, 4},

        {"P1810", "[Mercedes] Transmission adaptation fault: valve body pressure out of range", "Transmission",
         {"Worn valve body", "Contaminated transmission fluid", "Faulty pressure sensor"},
         {"Harsh shifting", "Check engine light"}, 3},

        {"P1970", "[Mercedes] Cooling fan control module malfunction", "Cooling System",
         {"Electronic fan module failure", "Fan wiring fault", "Blown fan fuse"},
         {"Engine overheating risk"}, 3}
    };

    int success_count = 0;
    for (const auto& dtc : manufacturer_dtcs) {
        if (add_dtc_code(dtc)) {
            success_count++;
        }
    }

    std::cout << "Добавлено кодов производителей: " << success_count
        << " из " << manufacturer_dtcs.size() << std::endl;
}

void Database::populate_default_dtc_codes() {
    std::cout << "Заполнение базы данных DTC кодами..." << std::endl;

    std::vector<DTCRecord> default_dtcs = {
        // P-коды (Powertrain)
        {"P0100", "Mass or Volume Air Flow Circuit Malfunction", "Powertrain",
         {"Faulty MAF sensor", "Open or short in MAF circuit", "Vacuum leak"},
         {"Poor acceleration", "Black smoke from exhaust", "Engine stall"}, 3},

        {"P0101", "Mass or Volume Air Flow Circuit Range/Performance Problem", "Powertrain",
         {"Dirty MAF sensor", "Vacuum leak", "Air intake leak"},
         {"Rough idle", "Poor fuel economy", "Engine hesitation"}, 3},

        {"P0102", "Mass or Volume Air Flow Circuit Low Input", "Powertrain",
         {"Faulty MAF sensor", "Open in MAF circuit", "Short to ground"},
         {"Check engine light", "Reduced power", "Poor performance"}, 4},

        {"P0103", "Mass or Volume Air Flow Circuit High Input", "Powertrain",
         {"Faulty MAF sensor", "Short to voltage", "ECU problem"},
         {"Rich fuel mixture", "Black smoke", "Catalyst damage"}, 4},

        {"P0110", "Intake Air Temperature Circuit Malfunction", "Powertrain",
         {"Faulty IAT sensor", "Wiring issue", "Connector corrosion"},
         {"Hard starting", "Poor fuel economy", "Rough idle"}, 2},

        {"P0115", "Engine Coolant Temperature Circuit Malfunction", "Powertrain",
         {"Faulty ECT sensor", "Wiring problem", "Thermostat stuck open"},
         {"Hard cold start", "Poor fuel economy", "Engine overheating"}, 3},

        {"P0116", "Engine Coolant Temperature Circuit Range/Performance", "Powertrain",
         {"Thermostat stuck", "Faulty sensor", "Low coolant level"},
         {"Temperature gauge incorrect", "Poor heater performance"}, 2},

        {"P0120", "Throttle/Pedal Position Sensor/Switch A Circuit Malfunction", "Powertrain",
         {"Faulty TPS", "Wiring issue", "Throttle body carbon buildup"},
         {"Poor acceleration", "Hesitation", "No throttle response"}, 4},

        {"P0121", "Throttle/Pedal Position Sensor/Switch A Circuit Range/Performance", "Powertrain",
         {"Faulty TPS", "Throttle plate stuck", "ECU issue"},
         {"Intermittent throttle response", "Limp mode"}, 3},

        {"P0122", "Throttle/Pedal Position Sensor/Switch A Circuit Low Input", "Powertrain",
         {"Short to ground", "Open circuit", "Faulty sensor"},
         {"No throttle response", "Limp mode"}, 4},

        {"P0123", "Throttle/Pedal Position Sensor/Switch A Circuit High Input", "Powertrain",
         {"Short to voltage", "Faulty sensor", "ECU problem"},
         {"Erratic throttle", "Limp mode"}, 4},

        {"P0130", "O2 Sensor Circuit Malfunction (Bank 1 Sensor 1)", "Powertrain",
         {"Faulty O2 sensor", "Exhaust leak", "Wiring issue"},
         {"Poor fuel economy", "Failed emissions", "Check engine light"}, 3},

        {"P0135", "O2 Sensor Heater Circuit Malfunction (Bank 1 Sensor 1)", "Powertrain",
         {"Faulty heater element", "Blown fuse", "Wiring issue"},
         {"Slow sensor response", "Rich mixture at cold start"}, 2},

        {"P0171", "System Too Lean (Bank 1)", "Powertrain",
         {"Vacuum leak", "Fuel pressure low", "MAF sensor dirty", "Injector clogged"},
         {"Rough idle", "Hesitation", "Poor fuel economy"}, 3},

        {"P0172", "System Too Rich (Bank 1)", "Powertrain",
         {"Fuel pressure high", "Injector leak", "MAF sensor faulty", "EVAP system issue"},
         {"Black smoke", "Strong fuel smell", "Poor fuel economy"}, 3},

        {"P0300", "Random/Multiple Cylinder Misfire Detected", "Powertrain",
         {"Spark plugs worn", "Ignition coils faulty", "Fuel injectors clogged",
          "Vacuum leak", "Low compression"},
         {"Engine vibration", "Loss of power", "Check engine light flashing"}, 4},

        {"P0301", "Cylinder 1 Misfire Detected", "Powertrain",
         {"Faulty spark plug", "Bad ignition coil", "Injector problem", "Low compression"},
         {"Rough idle", "Loss of power", "Engine shaking"}, 4},

        {"P0302", "Cylinder 2 Misfire Detected", "Powertrain",
         {"Faulty spark plug", "Bad ignition coil", "Injector problem", "Low compression"},
         {"Rough idle", "Loss of power", "Engine shaking"}, 4},

        {"P0303", "Cylinder 3 Misfire Detected", "Powertrain",
         {"Faulty spark plug", "Bad ignition coil", "Injector problem", "Low compression"},
         {"Rough idle", "Loss of power", "Engine shaking"}, 4},

        {"P0304", "Cylinder 4 Misfire Detected", "Powertrain",
         {"Faulty spark plug", "Bad ignition coil", "Injector problem", "Low compression"},
         {"Rough idle", "Loss of power", "Engine shaking"}, 4},

        {"P0400", "Exhaust Gas Recirculation Flow Malfunction", "Powertrain",
         {"EGR valve stuck", "EGR passages clogged", "Vacuum leak"},
         {"Pinging", "Increased emissions", "Poor fuel economy"}, 2},

        {"P0420", "Catalyst System Efficiency Below Threshold (Bank 1)", "Powertrain",
         {"Faulty catalytic converter", "Exhaust leak", "O2 sensor issue", "Engine misfire"},
         {"Failed emissions", "Check engine light", "Reduced performance"}, 3},

        {"P0440", "Evaporative Emission Control System Malfunction", "Powertrain",
         {"Loose gas cap", "EVAP system leak", "Purge valve stuck"},
         {"Fuel smell", "Check engine light"}, 1},

        {"P0442", "Evaporative Emission System Leak Detected (small leak)", "Powertrain",
         {"Loose gas cap", "Small EVAP leak", "Faulty vent valve"},
         {"Check engine light", "Failed emissions"}, 1},

        {"P0455", "Evaporative Emission System Leak Detected (large leak)", "Powertrain",
         {"Gas cap loose or missing", "Large EVAP leak", "EVAP hose disconnected"},
         {"Fuel smell", "Check engine light"}, 2},

        {"P0500", "Vehicle Speed Sensor Malfunction", "Powertrain",
         {"Faulty VSS", "Wiring issue", "Speedometer problem"},
         {"Speedometer not working", "Transmission shifting issues"}, 2},

        {"P0505", "Idle Control System Malfunction", "Powertrain",
         {"Faulty IAC valve", "Vacuum leak", "Throttle body dirty"},
         {"Incorrect idle speed", "Stalling", "Rough idle"}, 2},

        {"P0560", "System Voltage Malfunction", "Powertrain",
         {"Faulty alternator", "Weak battery", "Wiring issue"},
         {"Battery light on", "Electrical issues", "Hard starting"}, 2},

        {"P0600", "Serial Communication Link Malfunction", "Powertrain",
         {"CAN bus issue", "Faulty ECU", "Wiring problem"},
         {"Multiple warning lights", "Communication errors"}, 4},

        {"P0606", "ECU Processor Fault", "Powertrain",
         {"Internal ECU failure", "Software corruption"},
         {"Engine won't start", "Erratic behavior", "Multiple systems fail"}, 5},

        {"P0700", "Transmission Control System Malfunction", "Powertrain",
         {"Transmission ECU issue", "Communication error"},
         {"Transmission warning light", "Limp mode"}, 3},

        {"P0705", "Transmission Range Sensor Circuit Malfunction", "Powertrain",
         {"Faulty TR sensor", "Wiring issue", "Misadjusted shift linkage"},
         {"Incorrect gear display", "No crank in Park"}, 2},

         // C-коды (Chassis)
         {"C0035", "Left Front Wheel Speed Sensor", "Chassis",
          {"Faulty sensor", "Wiring issue", "Tone ring damaged"},
          {"ABS light on", "Traction control disabled"}, 3},

         {"C0040", "Right Front Wheel Speed Sensor", "Chassis",
          {"Faulty sensor", "Wiring issue", "Tone ring damaged"},
          {"ABS light on", "Traction control disabled"}, 3},

         {"C0045", "Left Rear Wheel Speed Sensor", "Chassis",
          {"Faulty sensor", "Wiring issue", "Tone ring damaged"},
          {"ABS light on", "Traction control disabled"}, 3},

         {"C0050", "Right Rear Wheel Speed Sensor", "Chassis",
          {"Faulty sensor", "Wiring issue", "Tone ring damaged"},
          {"ABS light on", "Traction control disabled"}, 3},

         {"C0110", "Pump Motor Circuit Malfunction", "Chassis",
          {"Faulty pump motor", "Relay issue", "Wiring problem"},
          {"ABS inoperative", "Brake warning light"}, 4},

         {"C0121", "Valve Relay Circuit Malfunction", "Chassis",
          {"Faulty relay", "Wiring issue", "ABS module problem"},
          {"ABS inoperative", "Warning lights"}, 3},

          // B-коды (Body)
          {"B1000", "ECU Malfunction", "Body",
           {"Internal module failure", "Software corruption"},
           {"System failure", "Warning lights"}, 4},

          {"B1200", "Climate Control Temperature Sensor Circuit", "Body",
           {"Faulty sensor", "Wiring issue", "HVAC module problem"},
           {"Incorrect temperature control", "HVAC issues"}, 1},

          {"B1320", "Driver Door Circuit Malfunction", "Body",
           {"Faulty door module", "Wiring issue", "Door lock actuator problem"},
           {"Door locks not working", "Window issues"}, 1},

          {"B1340", "Passenger Door Circuit Malfunction", "Body",
           {"Faulty door module", "Wiring issue", "Door lock actuator problem"},
           {"Door locks not working", "Window issues"}, 1},

          {"B1400", "Driver Seat Memory Function Malfunction", "Body",
           {"Seat module issue", "Motor problem", "Position sensor faulty"},
           {"Memory seat not working"}, 1},

          {"B1420", "Sunroof Control Module Malfunction", "Body",
           {"Faulty module", "Motor problem", "Wiring issue"},
           {"Sunroof not working"}, 1},

          {"B1500", "Passenger Presence System Malfunction", "Body",
           {"Sensor mat issue", "Airbag module problem", "Wiring fault"},
           {"Airbag warning light", "Passenger airbag disabled"}, 3},

          {"B2000", "Immobilizer System Malfunction", "Body",
           {"Key not recognized", "Immobilizer module fault", "Wiring issue"},
           {"Engine won't start", "Security light on"}, 4},

          {"B2100", "Key Transponder Not Programmed", "Body",
           {"Unprogrammed key", "Immobilizer issue"},
           {"Engine won't start", "Security light flashing"}, 3},

          {"B2200", "Steering Lock Malfunction", "Body",
           {"Steering lock motor fault", "Module issue", "Mechanical binding"},
           {"Key won't turn", "Steering locked"}, 3},

           // U-коды (Network)
           {"U0100", "Lost Communication With ECU/PCM", "Network",
            {"CAN bus issue", "ECU power problem", "Wiring fault", "ECU failure"},
            {"Multiple warning lights", "No start", "Gauges not working"}, 4},

           {"U0101", "Lost Communication With Transmission Control Module", "Network",
            {"CAN bus issue", "TCM power problem", "Wiring fault"},
            {"Transmission issues", "Limp mode"}, 3},

           {"U0102", "Lost Communication With Transfer Case Control Module", "Network",
            {"CAN bus issue", "Module power problem", "Wiring fault"},
            {"4WD issues", "Warning lights"}, 2},

           {"U0121", "Lost Communication With ABS Module", "Network",
            {"CAN bus issue", "ABS module power problem", "Wiring fault"},
            {"ABS light on", "ABS inoperative"}, 3},

           {"U0122", "Lost Communication With Vehicle Dynamics Control Module", "Network",
            {"CAN bus issue", "VDC module problem", "Wiring fault"},
            {"Stability control off", "Warning lights"}, 3},

           {"U0131", "Lost Communication With Power Steering Control Module", "Network",
            {"CAN bus issue", "EPS module problem", "Wiring fault"},
            {"Power steering reduced", "Warning lights"}, 3},

           {"U0140", "Lost Communication With Body Control Module", "Network",
            {"CAN bus issue", "BCM power problem", "Wiring fault"},
            {"Multiple electrical issues", "Warning lights"}, 3},

           {"U0155", "Lost Communication With Instrument Panel Cluster", "Network",
            {"CAN bus issue", "Cluster power problem", "Wiring fault"},
            {"Gauges not working", "No display"}, 2},

           {"U0164", "Lost Communication With HVAC Control Module", "Network",
            {"CAN bus issue", "HVAC module problem", "Wiring fault"},
            {"HVAC not working"}, 1},

           {"U0300", "Internal Control Module Software Incompatibility", "Network",
            {"Software version mismatch", "Module replaced with wrong part"},
            {"Multiple system issues", "Warning lights"}, 3},

           {"U0401", "Invalid Data Received From ECU/PCM", "Network",
            {"ECU malfunction", "CAN bus corrupted data"},
            {"Erratic behavior", "Warning lights"}, 3},

           {"U1000", "CAN Communication Bus Fault", "Network",
            {"Bus wiring issue", "Termination problem", "Module fault"},
            {"Communication errors", "Multiple warning lights"}, 3},

           {"U1001", "CAN Network Bus Off", "Network",
            {"Bus shorted", "Module causing bus off", "Wiring damage"},
            {"Complete communication loss", "Vehicle may not start"}, 4}
    };

    int success_count = 0;
    for (const auto& dtc : default_dtcs) {
        if (add_dtc_code(dtc)) {
            success_count++;
        }
    }

    std::cout << "База данных DTC кодов заполнена. Добавлено записей: " << success_count
        << " из " << default_dtcs.size() << std::endl;
}