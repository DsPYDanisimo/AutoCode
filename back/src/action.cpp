// back/src/action.cpp
#include <string>
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <map>
#include <vector>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <mutex>
#include "../include/ecu_detector.h"
#include "../include/obd_connector.h"
#include "../include/database.h"
#include "../include/json.hpp"
#include "../include/httplib.h"

using json = nlohmann::json;
ECUDETECTOR ecu_detector;
Database database;
std::mutex ecu_mutex; // Мьютекс для защиты доступа к ECU

// Вспомогательные функции
json dtc_to_json(const DTCCode& dtc);
json can_message_to_json(const CANMessage& msg);
std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
json dtc_record_to_json(const DTCRecord& record);

void setup_api_server(httplib::Server& server);

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // Переключаем stdout/stderr в бинарный режим чтобы UTF-8 байты
    // проходили без перекодировки C-рантаймом
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    std::cout << "=========================================================" << std::endl;
    std::cout << "Запуск сервера диагностики ЭБУ v2.0" << std::endl;
    std::cout << "Поддерживаемые протоколы: OBD-II, KWP2000, CAN, UDS" << std::endl;
    std::cout << "=========================================================" << std::endl;

    // Подключаемся к базе данных
    if (!database.connect("ecu_diagnostic.db")) {
        std::cerr << "❌ Ошибка подключения к БД: " << database.get_last_error() << std::endl;
        return 1;
    }
    std::cout << "✅ База данных подключена" << std::endl;

    // Проверяем наличие DTC кодов
    int dtc_count = database.get_dtc_count();
    std::cout << "✅ В базе данных DTC кодов: " << dtc_count << std::endl;

    // Если база пуста или почти пуста, добавляем расширенные коды
    if (dtc_count < 50) {
        std::cout << "✅ Добавление расширенных DTC кодов..." << std::endl;
        database.initialize_extended_dtc_database();
    }

    httplib::Server server;

    // Устанавливаем увеличенные таймауты для сервера
    server.set_read_timeout(120, 0);  // 120 секунд на чтение (ELM327 protocol detection)
    server.set_write_timeout(120, 0); // 120 секунд на запись
    server.set_keep_alive_max_count(100); // Увеличиваем максимальное количество keep-alive соединений
    server.set_keep_alive_timeout(30); // 30 секунд таймаут keep-alive
    server.set_payload_max_length(1024 * 1024); // 1MB максимальный размер payload

    setup_api_server(server);

    std::cout << "\n🌐 Сервер запущен на http://localhost:8080" << std::endl;
    std::cout << "API endpoints:" << std::endl;
    std::cout << "  GET  /api/connection/status - статус подключения" << std::endl;
    std::cout << "  GET  /api/ports/scan - сканирование портов (старый)" << std::endl;
    std::cout << "  GET  /api/ports/scan_all - сканирование всех типов портов" << std::endl;
    std::cout << "  POST /api/connect - подключение к ЭБУ" << std::endl;
    std::cout << "  POST /api/disconnect - отключение" << std::endl;
    std::cout << "  GET  /api/errors/read - чтение ошибок" << std::endl;
    std::cout << "  POST /api/errors/clear - очистка ошибок" << std::endl;
    std::cout << "  GET  /api/ecu/info - информация об ЭБУ" << std::endl;
    std::cout << "  GET  /api/live/data - данные в реальном времени" << std::endl;
    std::cout << "  GET  /api/protocols/supported - поддерживаемые протоколы" << std::endl;
    std::cout << "  GET  /api/can/monitor - мониторинг CAN шины" << std::endl;
    std::cout << "  GET  /api/uds/dids - список поддерживаемых DID" << std::endl;
    std::cout << "  GET  /api/dtc/all - все DTC коды из базы" << std::endl;
    std::cout << "  GET  /api/dtc/search - поиск DTC кодов" << std::endl;
    std::cout << "  GET  /api/dtc/{code} - информация о конкретном DTC" << std::endl;
    std::cout << "=========================================================" << std::endl;

    server.listen("0.0.0.0", 8080);

    return 0;
}

void setup_api_server(httplib::Server& server) {
    // Статус подключения - быстрый ответ без блокировок
    server.Get("/api/connection/status", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            ECUConnection status = ecu_detector.check_connection();

            response = {
                {"connected", status.is_connected},
                {"protocol", status.protocol},
                {"port", status.port},
                {"port_type", status.port_type},
                {"ecu_type", status.ecu_type},
                {"vin", status.vin},
                {"signal_quality", status.signal_quality},
                {"supported_protocols", ecu_detector.get_supported_protocols()}
            };

            if (!status.supported_pids.empty()) {
                response["supported_pids"] = status.supported_pids;
            }

            if (!status.diagnostic_data.empty()) {
                response["diagnostic_data"] = status.diagnostic_data;
            }
        }
        catch (const std::exception& e) {
            response = { {"connected", false}, {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Сканирование портов (старая версия)
    server.Get("/api/ports/scan", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            auto ports = ecu_detector.scan_ports();
            response = ports;
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Сканирование всех портов с детальной информацией
    // ecu_mutex не нужен — scan_all_ports только читает порты, не трогает состояние ECU
    server.Get("/api/ports/scan_all", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        json response = json::array();

        try {
            auto ports = ecu_detector.scan_all_ports();

            for (const auto& port : ports) {
                response.push_back({
                    {"name", port.name},
                    {"type", port.type},
                    {"description", port.description},
                    {"is_available", port.is_available},
                    {"device_path", port.device_path},
                    {"baud_rate", port.baud_rate},
                    {"host", port.host},
                    {"port", port.port}
                    });
            }
        }
        catch (const std::exception& e) {
            response = json::array();
        }

        res.set_content(response.dump(), "application/json");
        });

    // Подключение - УЛУЧШЕННАЯ ВЕРСИЯ
    server.Post("/api/connect", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        std::cout << "Получен запрос на подключение" << std::endl;

        try {
            auto j = json::parse(req.body);

            PortInfo port_info;
            port_info.name = j["name"];
            port_info.type = j.value("type", "serial");
            port_info.baud_rate = j.value("baud_rate", 115200);
            port_info.host = j.value("host", "");
            port_info.port = j.value("port", 0);
            port_info.device_path = j.value("device_path", "");

            std::cout << "Попытка подключения к " << port_info.name << " (" << port_info.type << ")" << std::endl;

            std::lock_guard<std::mutex> lock(ecu_mutex);

            // Сначала отключаемся, если уже были подключены
            if (ecu_detector.is_connected()) {
                std::cout << "Отключаем предыдущее соединение" << std::endl;
                ecu_detector.disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            ECUConnection result = ecu_detector.connect_to_port(port_info);

            response = {
                {"success", result.is_connected},
                {"protocol", result.protocol},
                {"port", result.port},
                {"port_type", result.port_type},
                {"ecu_info", result.ecu_type},
                {"vin", result.vin},
                {"signal_quality", result.signal_quality}
            };

            if (!result.is_connected) {
                response["error"] = "ELM327 адаптер не обнаружен на порту " + port_info.name +
                    ". Проверьте подключение адаптера и питание. "
                    "Протестированы скорости: 38400, 115200, 9600, 57600, 19200 бод.";
            }

            // Если подключились успешно, сохраняем информацию в БД
            if (result.is_connected && !result.vin.empty()) {
                ECUInfo db_info;
                db_info.vin = result.vin;
                db_info.ecu_type = result.ecu_type;
                db_info.protocol = result.protocol;
                database.update_ecu_info(result.vin, db_info);
                std::cout << "Подключение успешно, VIN: " << result.vin << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Ошибка подключения: " << e.what() << std::endl;
            response = { {"success", false}, {"error", "Connection failed"} };
        }
        catch (...) {
            response = { {"success", false}, {"error", "Unknown exception"} };
        }

        try {
            res.set_content(response.dump(), "application/json");
        } catch (...) {
            res.set_content("{\"success\":false,\"error\":\"Serialization error\"}", "application/json");
        }
        });

    // Отключение
    server.Post("/api/disconnect", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            ecu_detector.disconnect();
        }
        catch (...) {
            // Игнорируем ошибки при отключении
        }

        json response = { {"success", true} };
        res.set_content(response.dump(), "application/json");
        });

    // Чтение ошибок
    server.Get("/api/errors/read", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            auto errors = ecu_detector.read_real_errors();

            if (!errors.empty()) {
                json errors_array = json::array();
                for (const auto& dtc : errors) {
                    errors_array.push_back(dtc_to_json(dtc));
                }
                response = {
                    {"errors", errors_array},
                    {"count", errors.size()}
                };
            }
            else {
                response = {
                    {"errors", json::array()},
                    {"count", 0},
                    {"message", "No errors or not connected"}
                };
            }
        }
        catch (const std::exception& e) {
            response = { {"errors", json::array()}, {"count", 0}, {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Очистка ошибок
    server.Post("/api/errors/clear", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            bool success = ecu_detector.clear_errors();

            response = {
                {"success", success},
                {"message", success ? "Errors cleared successfully" : "Failed to clear errors"}
            };
        }
        catch (const std::exception& e) {
            response = { {"success", false}, {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Информация об ЭБУ
    server.Get("/api/ecu/info", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            if (ecu_detector.check_connection().is_connected) {
                auto info = ecu_detector.read_ecu_info();
                response = info;
            }
            else {
                response = { {"error", "Not connected"} };
            }
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Live данные
    server.Get("/api/live/data", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            auto data = ecu_detector.read_live_data();

            if (!data.empty()) {
                response = data;
            }
            else {
                response = { {"error", "Not connected or no data"} };
            }
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Поддерживаемые протоколы
    server.Get("/api/protocols/supported", [](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            response = {
                {"protocols", ecu_detector.get_supported_protocols()},
                {"current", ecu_detector.check_connection().protocol}
            };
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Мониторинг CAN шины
    server.Get("/api/can/monitor", [](const httplib::Request& req, httplib::Response& res) {
        int duration = 2000;
        auto duration_param = req.get_param_value("duration");
        if (!duration_param.empty()) {
            try {
                duration = std::stoi(duration_param);
            }
            catch (...) {}
        }

        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            auto messages = ecu_detector.monitor_can_bus(duration);

            json messages_array = json::array();
            for (const auto& msg : messages) {
                messages_array.push_back(can_message_to_json(msg));
            }

            response = {
                {"duration_ms", duration},
                {"message_count", messages.size()},
                {"messages", messages_array}
            };
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Активные CAN ID
    server.Get("/api/can/ids", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            auto ids = ecu_detector.get_active_can_ids();

            response = {
                {"ids", ids},
                {"count", ids.size()}
            };
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Идентификация CAN узлов
    server.Get("/api/can/identify", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            auto nodes = ecu_detector.identify_can_nodes();

            for (const auto& node : nodes) {
                response[std::to_string(node.first)] = node.second;
            }
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // UDS - список поддерживаемых DID
    server.Get("/api/uds/dids", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            std::lock_guard<std::mutex> lock(ecu_mutex);
            auto dids = ecu_detector.get_supported_dids();

            for (const auto& did : dids) {
                response[std::to_string(did.first)] = did.second;
            }
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // UDS - сброс ECU
    server.Post("/api/uds/reset", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            auto j = json::parse(req.body);
            uint8_t reset_type = j.value("type", 0x01);

            std::lock_guard<std::mutex> lock(ecu_mutex);
            bool success = ecu_detector.uds_ecu_reset(reset_type);

            response = {
                {"success", success},
                {"reset_type", reset_type}
            };
        }
        catch (const std::exception& e) {
            response = { {"success", false}, {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Получить все DTC коды
    server.Get("/api/dtc/all", [](const httplib::Request& req, httplib::Response& res) {
        json response = json::array();

        try {
            auto all_dtcs = database.get_all_dtc_codes();
            for (const auto& dtc : all_dtcs) {
                response.push_back(dtc_record_to_json(dtc));
            }
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Поиск DTC кодов
    server.Get("/api/dtc/search", [](const httplib::Request& req, httplib::Response& res) {
        std::string query = req.get_param_value("query");
        json response = json::array();

        try {
            auto results = database.search_dtc_codes(query);
            for (const auto& dtc : results) {
                response.push_back(dtc_record_to_json(dtc));
            }
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });

    // Получить информацию о DTC коде
    server.Get(R"(/api/dtc/([A-Z0-9]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string code = req.matches[1];
        json response;

        try {
            auto dtc = database.get_dtc_by_code(code);
            if (dtc.has_value()) {
                response = dtc_record_to_json(dtc.value());
            }
            else {
                response = { {"error", "DTC code not found"} };
            }
        }
        catch (const std::exception& e) {
            response = { {"error", e.what()} };
        }

        res.set_content(response.dump(), "application/json");
        });
}

// Вспомогательные функции
json dtc_to_json(const DTCCode& dtc) {
    return {
        {"code", dtc.code},
        {"description", dtc.description},
        {"status", dtc.status},
        {"severity", dtc.severity},
        {"timestamp", dtc.timestamp}
    };
}

json dtc_record_to_json(const DTCRecord& record) {
    return {
        {"code", record.code},
        {"description", record.description},
        {"system_type", record.system_type},
        {"possible_causes", record.possible_causes},
        {"symptoms", record.symptoms},
        {"severity", record.severity}
    };
}

json can_message_to_json(const CANMessage& msg) {
    std::stringstream data_ss;
    for (uint8_t byte : msg.data) {
        data_ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(byte) << " ";
    }

    return {
        {"id", msg.id},
        {"is_extended", msg.is_extended},
        {"data", data_ss.str()},
        {"dlc", msg.data.size()}
    };
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    for (uint8_t byte : bytes) {
        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(byte);
    }
    return ss.str();
}