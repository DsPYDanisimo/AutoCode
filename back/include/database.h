#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory>
#include <chrono>
#include <iostream>
#include <sqlite3.h>

// ��������� �������������� ��� MSVC
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

struct DTCRecord {
    std::string code;
    std::string description;
    std::string system_type;
    std::vector<std::string> possible_causes;
    std::vector<std::string> symptoms;
    int severity; // 1-5, ��� 5 - �����������
};

struct DTCInstance {
    std::string dtc_code;
    std::string description;
    std::string read_time;
    int mileage;
    std::string environment_data;
    std::string status; // active, pending, historical
};

struct ECUInfo {
    std::string vin;
    std::string ecu_type;
    std::string protocol;
    std::string software_version;
    std::string hardware_version;
    std::string manufacturer;
    std::string model;
    int year;
    std::string last_connection;
};

struct LiveData {
    std::string timestamp;
    int rpm;
    int speed;
    int coolant_temp;
    float throttle_position;
    float fuel_pressure;
    int intake_temp;
    float maf_rate;
    float voltage;
    std::string raw_data; // JSON ������ � ��������������� �������
};

class Database {
private:
    sqlite3* db_;
    std::string db_path_;
    bool is_connected_;

    bool execute_sql(const std::string& sql);
    bool execute_sql_with_params(const std::string& sql, const std::vector<std::string>& params);
    void create_tables_if_not_exist();
    bool is_dtc_table_empty();
    void populate_default_dtc_codes();

    // ��������������� ������� ��� ������ � SQLite
    static int callback_select_count(void* data, int argc, char** argv, char** azColName);
    static int callback_select_dtc(void* data, int argc, char** argv, char** azColName);
    static int callback_select_history(void* data, int argc, char** argv, char** azColName);
    static int callback_select_ecu(void* data, int argc, char** argv, char** azColName);
    static int callback_select_live(void* data, int argc, char** argv, char** azColName);
    static int callback_select_all_dtc(void* data, int argc, char** argv, char** azColName);

public:
    Database();
    ~Database();

    // ����������� � ���� ������ (SQLite ������ "���������" � �����)
    bool connect(const std::string& db_path = "ecu_diagnostic.db");
    void disconnect();
    bool is_connected() const { return is_connected_; }
    std::string get_db_path() const { return db_path_; }

    // DTC ������
    bool add_dtc_code(const DTCRecord& dtc);
    bool add_dtc_codes_batch(const std::vector<DTCRecord>& dtc_list);
    std::optional<DTCRecord> get_dtc_by_code(const std::string& code);
    std::vector<DTCRecord> search_dtc_codes(const std::string& query);
    std::vector<DTCRecord> get_all_dtc_codes();
    bool delete_dtc_code(const std::string& code);
    bool update_dtc_code(const DTCRecord& dtc);
    int get_dtc_count();

    // ������� ������
    bool add_to_history(const std::string& dtc_code, const std::string& vin,
        int mileage, const std::string& environment_data = "{}");
    std::vector<DTCInstance> get_history(const std::string& vin, int limit = 100);
    bool clear_history(const std::string& vin = "");

    // ECU ����������
    bool update_ecu_info(const std::string& vin, const ECUInfo& info);
    std::optional<ECUInfo> get_ecu_info(const std::string& vin);
    std::vector<std::string> get_all_vin();

    // Live ������
    bool log_live_data(const std::string& vin, const LiveData& data);
    std::vector<LiveData> get_live_data_history(const std::string& vin,
        const std::string& from_time,
        const std::string& to_time);

    // ������� ������ �������
    bool cleanup_old_records(int days_to_keep = 30);

    // �������� ��������� ������
    std::string get_last_error() const { return last_error_; }

    // ������������� ���� � ������������ DTC ������
    void initialize_extended_dtc_database();
    void initialize_manufacturer_dtc_database();

private:
    std::string last_error_;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // DATABASE_H