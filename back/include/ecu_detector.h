#ifndef ECU_DETECTOR_H
#define ECU_DETECTOR_H

// ВАЖНО: Эти определения должны быть ДО любых включений Windows.h
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // Уменьшает размер Windows.h и исключает конфликты
#endif
#ifndef NOMINMAX
#define NOMINMAX  // Отключает макросы min/max
#endif
#include <windows.h>
#include <winsock2.h>  // ДОЛЖЕН быть перед windows.h
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#endif

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <functional>
#include <chrono>
#include <mutex>

// Определяем INVALID_SOCKET и socket_t для кроссплатформенности
#ifdef _WIN32
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SOCKET)(~0)
#endif
typedef SOCKET socket_t;
#else
#define INVALID_SOCKET -1
typedef int socket_t;
#endif

// Структура порта с информацией о типе
struct PortInfo {
    std::string name;
    std::string type;
    std::string description;
    bool is_available;
    std::string device_path;
    int baud_rate;
    std::string host;
    int port;

    PortInfo() : is_available(false), baud_rate(115200), port(0) {}
};

// Структура CAN сообщения
struct CANMessage {
    uint32_t id;
    bool is_extended;
    std::vector<uint8_t> data;
    std::chrono::system_clock::time_point timestamp;

    CANMessage() : id(0), is_extended(false), timestamp(std::chrono::system_clock::now()) {}
    CANMessage(uint32_t _id, const std::vector<uint8_t>& _data, bool _is_extended = false)
        : id(_id), is_extended(_is_extended), data(_data), timestamp(std::chrono::system_clock::now()) {
    }
};

// Структура подключения к ЭБУ
struct ECUConnection {
    bool is_connected;
    std::string protocol;
    std::string port;
    std::string port_type;
    std::string ecu_type;
    std::string vin;
    std::vector<std::string> supported_pids;
    std::map<std::string, std::string> diagnostic_data;
    std::map<uint16_t, std::string> supported_services;
    std::string connection_time;
    int signal_quality;

    ECUConnection() : is_connected(false), signal_quality(0) {}
};

// Структура кода ошибки
struct DTCCode {
    std::string code;
    std::string description;
    std::string status;
    std::vector<std::string> possible_causes;
    std::string timestamp;
    uint8_t severity;
    std::vector<uint8_t> environment_data;

    DTCCode() : severity(0) {}
};

// Структура для UDS параметров
struct UDSParameter {
    uint16_t did;
    std::string name;
    std::string unit;
    float min_value;
    float max_value;
    std::string formula;
    uint8_t access_type;
};

// Сервисы UDS
struct UDSServices {
    static constexpr uint8_t DIAGNOSTIC_SESSION_CONTROL = 0x10;
    static constexpr uint8_t ECU_RESET = 0x11;
    static constexpr uint8_t SECURITY_ACCESS = 0x27;
    static constexpr uint8_t TESTER_PRESENT = 0x3E;
    static constexpr uint8_t READ_DATA_BY_IDENTIFIER = 0x22;
    static constexpr uint8_t WRITE_DATA_BY_IDENTIFIER = 0x2E;
    static constexpr uint8_t CLEAR_DIAGNOSTIC_INFO = 0x14;
    static constexpr uint8_t READ_DTC_INFO = 0x19;
    static constexpr uint8_t ROUTINE_CONTROL = 0x31;
    static constexpr uint8_t REQUEST_DOWNLOAD = 0x34;
    static constexpr uint8_t TRANSFER_DATA = 0x36;
    static constexpr uint8_t REQUEST_TRANSFER_EXIT = 0x37;
};

class OBDConnector;

class ECUDETECTOR {
private:
    bool is_connected_;
    std::string current_port_;
    std::string current_port_type_;
    std::string current_protocol_;
    void* connection_handle_;
    std::unique_ptr<OBDConnector> obd_connector_;
    std::mutex connection_mutex_;

    socket_t tcp_socket_;  // Используем унифицированный тип socket_t

    // Кэшированные данные для уменьшения нагрузки
    std::string cached_vin_;
    std::chrono::steady_clock::time_point last_vin_read_;
    std::map<std::string, double> cached_live_data_;
    std::chrono::steady_clock::time_point last_live_read_;
    std::vector<DTCCode> cached_dtc_codes_;
    std::chrono::steady_clock::time_point last_dtc_read_;
    static constexpr int CACHE_DURATION_MS = 1000; // Кэшируем на 1 секунду

    bool try_obd2_connection(const std::string& port);
    bool try_can_connection(const std::string& port);
    bool try_kwp2000_connection(const std::string& port);
    bool try_uds_connection(const std::string& port);

    bool connect_tcp(const std::string& host, int port);
    bool try_obd2_tcp_connection(const std::string& host, int port);
    bool send_tcp_command(const std::string& cmd, std::string& response, int timeout_ms = 2000);

    bool initialize_can(const std::string& interface);
    bool send_can_diagnostic_request(uint32_t id, const std::vector<uint8_t>& data);
    std::map<uint32_t, std::vector<uint8_t>> discover_can_nodes();

    bool initialize_uds(const std::string& interface);
    uint16_t uds_get_vin_did();

    bool initialize_kwp2000();
    bool kwp2000_handshake();
    std::vector<uint8_t> kwp2000_send_command(const std::vector<uint8_t>& command);
    uint16_t kwp2000_calculate_checksum(const std::vector<uint8_t>& data);

public:
    ECUDETECTOR();
    ~ECUDETECTOR();

    std::vector<PortInfo> scan_all_ports();
    std::vector<std::string> scan_ports();
    ECUConnection connect_to_port(const PortInfo& port_info);
    ECUConnection connect_to_ecu(const std::string& port);
    std::string detect_protocol(const std::string& port);
    ECUConnection check_connection();
    std::vector<DTCCode> read_real_errors();
    std::map<std::string, double> read_live_data();
    std::map<std::string, std::string> read_ecu_info();
    void disconnect();
    bool clear_errors();

    std::vector<PortInfo> scan_serial_ports();
    std::vector<PortInfo> scan_tcp_ports(const std::vector<std::pair<std::string, int>>& common_ports);
    std::vector<PortInfo> scan_bluetooth_ports();

    bool kwp2000_security_access(uint16_t key);
    bool kwp2000_write_data(uint16_t param_id, const std::vector<uint8_t>& data);

    std::vector<CANMessage> monitor_can_bus(int duration_ms = 2000);
    std::vector<uint32_t> get_active_can_ids();
    std::map<uint32_t, std::string> identify_can_nodes();
    bool start_can_monitoring(std::function<void(const CANMessage&)> callback);
    void stop_can_monitoring();
    bool set_can_filter(uint32_t id, uint32_t mask);
    bool send_can_message(uint32_t id, const std::vector<uint8_t>& data, bool is_extended = false);
    std::vector<CANMessage> read_can_messages(int timeout_ms = 1000);
    bool test_can_bus();
    std::string identify_node_by_id(uint32_t id);

    std::map<uint16_t, std::string> get_supported_dids();
    bool uds_ecu_reset(uint8_t reset_type);
    std::map<uint16_t, std::vector<DTCCode>> read_extended_dtc_info();
    bool uds_flash_ecu(const std::string& firmware_path);
    std::vector<uint8_t> uds_read_memory(uint32_t address, uint32_t size);
    bool uds_write_memory(uint32_t address, const std::vector<uint8_t>& data);
    bool uds_discover_nodes();

    std::vector<std::string> get_supported_protocols() const;
    std::string get_protocol_description(const std::string& protocol) const;

    static std::string int_to_hex(unsigned int value);
    bool is_connected() const { return is_connected_; }
    std::string get_current_protocol() const { return current_protocol_; }
    std::string get_current_port() const { return current_port_; }

    // Методы для очистки кэша
    void clear_cache() {
        cached_vin_.clear();
        cached_live_data_.clear();
        cached_dtc_codes_.clear();
    }
};

enum class Protocol { NONE, OBD2, KWP2000, CAN, UDS };

#endif