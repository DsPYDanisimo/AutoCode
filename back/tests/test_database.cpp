// test_database.cpp — тесты класса Database (in-memory SQLite, без файлов)
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "database.h"

// =============================================================================
// Фикстура: открывает базу в памяти перед каждым тестом
// =============================================================================

class DatabaseTest : public ::testing::Test {
protected:
    Database db;

    void SetUp() override {
        // ":memory:" — SQLite in-memory database, изолированная для каждого теста
        ASSERT_TRUE(db.connect(":memory:"))
            << "Не удалось подключиться к in-memory БД";
    }

    void TearDown() override {
        db.disconnect();
    }

    // Вспомогательный метод: создаёт и добавляет тестовый DTC код
    DTCRecord make_dtc(const std::string& code,
                       const std::string& desc      = "Test description",
                       const std::string& sys       = "Powertrain",
                       int                severity  = 3) {
        DTCRecord r;
        r.code        = code;
        r.description = desc;
        r.system_type = sys;
        r.severity    = severity;
        return r;
    }

    // Создаёт тестовый ECUInfo
    ECUInfo make_ecu_info(const std::string& ecu_type  = "Bosch ME7",
                          const std::string& protocol  = "OBD-II",
                          const std::string& sw_ver    = "1.0.0",
                          const std::string& hw_ver    = "A1",
                          int                year      = 2020) {
        ECUInfo info;
        info.vin              = "";   // задаётся снаружи
        info.ecu_type         = ecu_type;
        info.protocol         = protocol;
        info.software_version = sw_ver;
        info.hardware_version = hw_ver;
        info.manufacturer     = "Bosch";
        info.model            = "ME7.5";
        info.year             = year;
        return info;
    }
};

// =============================================================================
// Подключение и состояние
// =============================================================================

TEST_F(DatabaseTest, ConnectToMemorySucceeds) {
    EXPECT_TRUE(db.is_connected());
}

TEST_F(DatabaseTest, DbPathIsMemory) {
    EXPECT_EQ(db.get_db_path(), ":memory:");
}

TEST_F(DatabaseTest, NotConnectedBeforeConnect) {
    Database fresh;
    EXPECT_FALSE(fresh.is_connected());
}

TEST_F(DatabaseTest, DisconnectSetsNotConnected) {
    db.disconnect();
    EXPECT_FALSE(db.is_connected());
}

TEST_F(DatabaseTest, GetLastErrorEmptyOnSuccess) {
    // После успешного подключения ошибок нет
    EXPECT_TRUE(db.get_last_error().empty());
}

// =============================================================================
// DTC кодов в базе после инициализации
// =============================================================================

TEST_F(DatabaseTest, DtcCountPositiveAfterConnect) {
    // connect() → create_tables_if_not_exist() → populate_default_dtc_codes()
    EXPECT_GT(db.get_dtc_count(), 0);
}

TEST_F(DatabaseTest, GetDtcCountReturnsZeroWhenNotConnected) {
    Database fresh;
    EXPECT_EQ(fresh.get_dtc_count(), 0);
}

// =============================================================================
// add_dtc_code / get_dtc_by_code
// =============================================================================

TEST_F(DatabaseTest, AddDtcCodeReturnsTrueAndIsRetrievable) {
    DTCRecord r = make_dtc("T0001", "Тестовая ошибка");
    ASSERT_TRUE(db.add_dtc_code(r));

    auto found = db.get_dtc_by_code("T0001");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->code,        "T0001");
    EXPECT_EQ(found->description, "Тестовая ошибка");
}

TEST_F(DatabaseTest, GetDtcByCodeNonExistentReturnsNullopt) {
    auto result = db.get_dtc_by_code("XYZXYZ");
    EXPECT_FALSE(result.has_value());
}

TEST_F(DatabaseTest, AddDtcCodeSeverityRoundTrip) {
    DTCRecord r = make_dtc("T0002", "Severity test", "Chassis", 5);
    db.add_dtc_code(r);

    auto found = db.get_dtc_by_code("T0002");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->severity, 5);
}

TEST_F(DatabaseTest, AddDtcCodeSystemTypeRoundTrip) {
    DTCRecord r = make_dtc("T0003", "Network error", "Network", 4);
    db.add_dtc_code(r);

    auto found = db.get_dtc_by_code("T0003");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->system_type, "Network");
}

TEST_F(DatabaseTest, AddDtcCodeWithCausesRoundTrip) {
    DTCRecord r = make_dtc("T0004");
    r.possible_causes = {"Причина 1", "Причина 2", "Причина 3"};
    db.add_dtc_code(r);

    auto found = db.get_dtc_by_code("T0004");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->possible_causes.size(), 3u);
    EXPECT_EQ(found->possible_causes[0], "Причина 1");
    EXPECT_EQ(found->possible_causes[2], "Причина 3");
}

TEST_F(DatabaseTest, AddDtcCodeWithSymptomsRoundTrip) {
    DTCRecord r = make_dtc("T0005");
    r.symptoms = {"Симптом A", "Симптом B"};
    db.add_dtc_code(r);

    auto found = db.get_dtc_by_code("T0005");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->symptoms.size(), 2u);
    EXPECT_EQ(found->symptoms[1], "Симптом B");
}

TEST_F(DatabaseTest, AddDtcCodeDuplicateOverwritesViaInsertOrReplace) {
    DTCRecord r1 = make_dtc("T0006", "Старое описание");
    db.add_dtc_code(r1);

    DTCRecord r2 = make_dtc("T0006", "Новое описание");
    EXPECT_TRUE(db.add_dtc_code(r2)); // INSERT OR REPLACE — должно успешно заменить

    auto found = db.get_dtc_by_code("T0006");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->description, "Новое описание");
}

// =============================================================================
// delete_dtc_code
// =============================================================================

TEST_F(DatabaseTest, DeleteDtcCodeRemovesIt) {
    db.add_dtc_code(make_dtc("T0010"));
    ASSERT_TRUE(db.get_dtc_by_code("T0010").has_value());

    EXPECT_TRUE(db.delete_dtc_code("T0010"));
    EXPECT_FALSE(db.get_dtc_by_code("T0010").has_value());
}

TEST_F(DatabaseTest, DeleteNonExistentCodeReturnsDone) {
    // SQLite DELETE на несуществующую запись завершается SQLITE_DONE
    EXPECT_TRUE(db.delete_dtc_code("DOESNOTEXIST"));
}

TEST_F(DatabaseTest, DeleteReducesDtcCount) {
    db.add_dtc_code(make_dtc("T0011"));
    int before = db.get_dtc_count();
    db.delete_dtc_code("T0011");
    EXPECT_EQ(db.get_dtc_count(), before - 1);
}

// =============================================================================
// update_dtc_code
// =============================================================================

TEST_F(DatabaseTest, UpdateDtcCodeChangesDescription) {
    db.add_dtc_code(make_dtc("T0020", "Первоначальное"));

    DTCRecord upd = make_dtc("T0020", "Обновлённое", "Body", 2);
    EXPECT_TRUE(db.update_dtc_code(upd));

    auto found = db.get_dtc_by_code("T0020");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->description, "Обновлённое");
    EXPECT_EQ(found->system_type, "Body");
    EXPECT_EQ(found->severity,    2);
}

TEST_F(DatabaseTest, UpdateDtcCodeUpdatesCauses) {
    db.add_dtc_code(make_dtc("T0021"));

    DTCRecord upd = make_dtc("T0021");
    upd.possible_causes = {"Новая причина"};
    db.update_dtc_code(upd);

    auto found = db.get_dtc_by_code("T0021");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->possible_causes.size(), 1u);
    EXPECT_EQ(found->possible_causes[0], "Новая причина");
}

// =============================================================================
// search_dtc_codes
// =============================================================================

TEST_F(DatabaseTest, SearchByCodePrefix) {
    db.add_dtc_code(make_dtc("P1234", "Fuel injector fault"));
    db.add_dtc_code(make_dtc("P1235", "Fuel pressure low"));

    auto results = db.search_dtc_codes("P123");
    EXPECT_GE(results.size(), 2u);
}

TEST_F(DatabaseTest, SearchByDescriptionKeyword) {
    db.add_dtc_code(make_dtc("T0030", "Oxygen sensor malfunction"));

    auto results = db.search_dtc_codes("oxygen");
    EXPECT_GE(results.size(), 1u);
    bool found = false;
    for (auto& r : results) {
        if (r.code == "T0030") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(DatabaseTest, SearchNoMatchReturnsEmpty) {
    auto results = db.search_dtc_codes("XYZZYNOTEXIST12345");
    EXPECT_TRUE(results.empty());
}

TEST_F(DatabaseTest, SearchIsCaseInsensitiveOrReturnsResults) {
    db.add_dtc_code(make_dtc("T0031", "Throttle Position Sensor"));
    // Проверяем что хотя бы один из вариантов находит запись
    auto r1 = db.search_dtc_codes("T0031");
    EXPECT_GE(r1.size(), 1u);
}

// =============================================================================
// get_all_dtc_codes
// =============================================================================

TEST_F(DatabaseTest, GetAllDtcCodesReturnsNonEmpty) {
    EXPECT_GT(db.get_all_dtc_codes().size(), 0u);
}

TEST_F(DatabaseTest, GetAllDtcCodesContainsAddedCode) {
    db.add_dtc_code(make_dtc("T0040", "Batch test"));
    auto all = db.get_all_dtc_codes();
    bool found = false;
    for (auto& r : all) {
        if (r.code == "T0040") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

// =============================================================================
// add_dtc_codes_batch
// =============================================================================

TEST_F(DatabaseTest, BatchAddReturnsTrueAndIncreasesCount) {
    int before = db.get_dtc_count();
    std::vector<DTCRecord> batch = {
        make_dtc("B0001", "Batch 1"),
        make_dtc("B0002", "Batch 2"),
        make_dtc("B0003", "Batch 3"),
    };
    EXPECT_TRUE(db.add_dtc_codes_batch(batch));
    EXPECT_EQ(db.get_dtc_count(), before + 3);
}

// =============================================================================
// ECU Info
// =============================================================================

TEST_F(DatabaseTest, UpdateEcuInfoReturnsTrueAndIsRetrievable) {
    ECUInfo info = make_ecu_info();
    ASSERT_TRUE(db.update_ecu_info("1HGBH41JXMN109186", info));

    auto result = db.get_ecu_info("1HGBH41JXMN109186");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ecu_type,         "Bosch ME7");
    EXPECT_EQ(result->protocol,         "OBD-II");
    EXPECT_EQ(result->software_version, "1.0.0");
    EXPECT_EQ(result->hardware_version, "A1");
    EXPECT_EQ(result->year,             2020);
}

TEST_F(DatabaseTest, GetEcuInfoNonExistentReturnsNullopt) {
    auto result = db.get_ecu_info("NOTAVIN0000000000");
    EXPECT_FALSE(result.has_value());
}

TEST_F(DatabaseTest, UpdateEcuInfoTwiceOverwrites) {
    db.update_ecu_info("TESTVIN00000000001", make_ecu_info("ME7", "OBD-II", "1.0", "A1", 2020));
    ECUInfo updated = make_ecu_info("ME9", "CAN", "2.0", "B2", 2022);
    db.update_ecu_info("TESTVIN00000000001", updated);

    auto result = db.get_ecu_info("TESTVIN00000000001");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ecu_type, "ME9");
    EXPECT_EQ(result->year,     2022);
}

TEST_F(DatabaseTest, GetAllVinReturnsAddedVin) {
    db.update_ecu_info("VIN0000000000001", make_ecu_info());
    auto vins = db.get_all_vin();
    bool found = std::find(vins.begin(), vins.end(), "VIN0000000000001") != vins.end();
    EXPECT_TRUE(found);
}

// =============================================================================
// DTC History
// =============================================================================

TEST_F(DatabaseTest, AddToHistoryReturnsTrueForKnownDtc) {
    // Используем код, добавленный вручную, чтобы FK не провалился
    db.add_dtc_code(make_dtc("H0001"));
    EXPECT_TRUE(db.add_to_history("H0001", "VIN_HIST_01", 50000));
}

TEST_F(DatabaseTest, GetHistoryReturnsAddedRecord) {
    db.add_dtc_code(make_dtc("H0002", "History test"));
    db.add_to_history("H0002", "VIN_HIST_02", 75000);

    auto history = db.get_history("VIN_HIST_02");
    EXPECT_FALSE(history.empty());
    EXPECT_EQ(history[0].dtc_code, "H0002");
    EXPECT_EQ(history[0].mileage,  75000);
}

TEST_F(DatabaseTest, GetHistoryForUnknownVinReturnsEmpty) {
    auto history = db.get_history("VIN_NOBODY_0000000");
    EXPECT_TRUE(history.empty());
}

TEST_F(DatabaseTest, AddToHistoryDuplicateNotInsertedTwice) {
    db.add_dtc_code(make_dtc("H0003"));
    db.add_to_history("H0003", "VIN_DUP_01", 10000);
    db.add_to_history("H0003", "VIN_DUP_01", 10001); // второй раз — не должен дублироваться

    auto history = db.get_history("VIN_DUP_01");
    EXPECT_EQ(history.size(), 1u);
}

TEST_F(DatabaseTest, ClearHistoryForSpecificVin) {
    db.add_dtc_code(make_dtc("H0004"));
    db.add_to_history("H0004", "VIN_CLR_01", 5000);

    EXPECT_TRUE(db.clear_history("VIN_CLR_01"));
    EXPECT_TRUE(db.get_history("VIN_CLR_01").empty());
}

TEST_F(DatabaseTest, ClearHistoryAllVins) {
    db.add_dtc_code(make_dtc("H0005"));
    db.add_dtc_code(make_dtc("H0006"));
    db.add_to_history("H0005", "VIN_ALL_01", 1000);
    db.add_to_history("H0006", "VIN_ALL_02", 2000);

    EXPECT_TRUE(db.clear_history()); // без аргумента — все записи
    EXPECT_TRUE(db.get_history("VIN_ALL_01").empty());
    EXPECT_TRUE(db.get_history("VIN_ALL_02").empty());
}

// =============================================================================
// Live Data
// =============================================================================

TEST_F(DatabaseTest, LogLiveDataReturnsTrueAfterEcuInfoAdded) {
    // live_data_log FK ссылается на ecu_info.vin
    db.update_ecu_info("VIN_LIVE_000000001", make_ecu_info());

    LiveData ld;
    ld.rpm              = 1500;
    ld.speed            = 60;
    ld.coolant_temp     = 90;
    ld.throttle_position = 25.0f;
    ld.fuel_pressure    = 3.5f;
    ld.intake_temp      = 30;
    ld.maf_rate         = 15.0f;
    ld.voltage          = 12.5f;
    ld.raw_data         = "{}";

    EXPECT_TRUE(db.log_live_data("VIN_LIVE_000000001", ld));
}

TEST_F(DatabaseTest, GetLiveDataHistoryReturnsLoggedData) {
    db.update_ecu_info("VIN_LIVE_000000002", make_ecu_info());

    LiveData ld;
    ld.rpm     = 3000;
    ld.speed   = 120;
    ld.voltage = 14.0f;
    ld.raw_data = "{}";
    db.log_live_data("VIN_LIVE_000000002", ld);

    // Временной диапазон охватывающий всё: с прошлого до будущего
    auto records = db.get_live_data_history(
        "VIN_LIVE_000000002",
        "2000-01-01 00:00:00",
        "2099-12-31 23:59:59");

    EXPECT_FALSE(records.empty());
    EXPECT_EQ(records[0].rpm,   3000);
    EXPECT_EQ(records[0].speed, 120);
}

TEST_F(DatabaseTest, GetLiveDataHistoryEmptyForUnknownVin) {
    auto records = db.get_live_data_history(
        "VIN_NOBODY_LIVE0000",
        "2000-01-01 00:00:00",
        "2099-12-31 23:59:59");
    EXPECT_TRUE(records.empty());
}

// =============================================================================
// cleanup_old_records
// =============================================================================

TEST_F(DatabaseTest, CleanupOldRecordsDoesNotCrash) {
    // Просто убеждаемся, что функция выполняется без исключений
    EXPECT_NO_THROW(db.cleanup_old_records(30));
    EXPECT_NO_THROW(db.cleanup_old_records(0));
}

// =============================================================================
// Целостность: операции на отключённой БД
// =============================================================================

TEST_F(DatabaseTest, OperationsOnDisconnectedDbReturnSafeValues) {
    db.disconnect();

    EXPECT_EQ(db.get_dtc_count(), 0);
    EXPECT_FALSE(db.add_dtc_code(make_dtc("X0001")));
    EXPECT_FALSE(db.get_dtc_by_code("X0001").has_value());
    EXPECT_TRUE(db.get_all_dtc_codes().empty());
    EXPECT_TRUE(db.search_dtc_codes("test").empty());
    EXPECT_FALSE(db.get_ecu_info("VIN_TEST_00000001").has_value());
    EXPECT_TRUE(db.get_history("VIN_TEST_00000001").empty());
}
