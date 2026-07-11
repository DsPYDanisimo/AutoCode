#ifndef OBD_CONNECTOR_H
#define OBD_CONNECTOR_H

// �����: ��� ����������� ������ ���� �� ����� ���������
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <stdexcept>
#include "serial_port.h"
#include "ecu_detector.h"

// Поставщик ключа Security Access (0x27, seed -> key). Алгоритм разблокировки
// специфичен для производителя/ЭБУ и является закрытой информацией — реализация
// должна подключаться отдельно (см. NoKeyProvider ниже).
class ISecurityKeyProvider {
public:
    virtual ~ISecurityKeyProvider() = default;
    virtual std::vector<uint8_t> compute_key(uint8_t access_type, const std::vector<uint8_t>& seed) = 0;
};

// Провайдер по умолчанию: явно сообщает об отсутствии алгоритма, а не подставляет
// случайный/угаданный ключ.
class NoKeyProvider : public ISecurityKeyProvider {
public:
    std::vector<uint8_t> compute_key(uint8_t access_type, const std::vector<uint8_t>& seed) override {
        (void)access_type;
        (void)seed;
        throw std::runtime_error(
            "Security Access: не настроен провайдер ключа (ISecurityKeyProvider). "
            "Алгоритм seed->key специфичен для производителя ЭБУ и должен быть подключён отдельно.");
    }
};

// ���������� INVALID_SOCKET ��� ��������������������
#ifdef _WIN32
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SOCKET)(~0)
#endif
typedef SOCKET socket_t;
#else
#define INVALID_SOCKET -1
typedef int socket_t;
#endif

// ��������� ��� OBD ����������
struct OBDParameter {
    std::string pid;
    std::string name;
    std::string unit;
    double min_value;
    double max_value;
    std::string formula;

    OBDParameter() : min_value(0), max_value(0) {}
    OBDParameter(const std::string& p, const std::string& n, const std::string& u,
        double min, double max, const std::string& f)
        : pid(p), name(n), unit(u), min_value(min), max_value(max), formula(f) {
    }
};

// ��������� ��� KWP2000 ����������
struct KWP2000Parameter {
    uint16_t id;
    std::string name;
    std::string unit;
    float min_value;
    float max_value;
    std::string formula;

    KWP2000Parameter() : id(0), min_value(0), max_value(0) {}
};

// ��������� ��� KWP2000 �������
struct KWP2000Packet {
    uint8_t header;
    uint8_t target_addr;
    uint8_t source_addr;
    uint8_t service_id;
    std::vector<uint8_t> data;
    uint8_t checksum;

    std::vector<uint8_t> to_bytes() const {
        std::vector<uint8_t> bytes;
        bytes.push_back(header);
        bytes.push_back(target_addr);
        bytes.push_back(source_addr);
        bytes.push_back(service_id);
        bytes.insert(bytes.end(), data.begin(), data.end());
        bytes.push_back(checksum);
        return bytes;
    }

    bool is_valid() const {
        uint8_t calc_checksum = 0;
        calc_checksum += header + target_addr + source_addr + service_id;
        for (uint8_t byte : data) {
            calc_checksum += byte;
        }
        return calc_checksum == checksum;
    }

    KWP2000Packet() : header(0), target_addr(0), source_addr(0), service_id(0), checksum(0) {}
};

// ��������� ��� UDS �������
struct UDSPacket {
    uint8_t service_id;
    std::vector<uint8_t> data;

    bool has_negative_response() const { return service_id >= 0x7F; }
    uint8_t get_negative_response_code() const {
        return has_negative_response() && data.size() > 0 ? data[0] : 0;
    }

    UDSPacket() : service_id(0) {}
};

// �������� ����� OBD ����������
class OBDConnector {
public:
    OBDConnector();
    ~OBDConnector();

    // ������� ������ �����������
    bool connect(const std::string& port, unsigned int baud_rate = 115200);
    bool connect_tcp(const std::string& host, int port);
    void disconnect();
    bool send_keep_alive();
    bool is_connected() const;

    // ������ ��� TCP ������
    void set_tcp_socket(socket_t sock);
    socket_t get_tcp_socket() const;
    bool is_tcp_mode() const;

    // ELM327 �������
    bool initialize_adapter();
    bool set_protocol(int protocol_id);
    std::string send_at_command(const std::string& command);
    std::string send_obd_command(const std::string& pid);
    std::string send_command(const std::string& command, int timeout_ms = 2000);

    // TCP ����������� �������
    std::string send_tcp_command(const std::string& cmd, int timeout_ms = 2000);
    bool send_tcp_raw(const std::string& data);

    // OBD-II ������
    double read_parameter(const std::string& pid);
    std::map<std::string, double> read_multiple_parameters(const std::vector<std::string>& pids);
    std::vector<DTCCode> read_dtc_codes();
    bool clear_dtc_codes();
    std::string read_vin();
    std::vector<std::string> get_supported_pids();

    // KWP2000 ������
    bool initialize_kwp2000();
    std::vector<uint8_t> send_kwp2000_command(const std::vector<uint8_t>& command, int timeout_ms = 2000);
    std::vector<DTCCode> read_kwp2000_dtc();
    bool clear_kwp2000_dtc();
    std::string read_kwp2000_vin();
    std::map<uint16_t, float> read_kwp2000_parameters(const std::vector<uint16_t>& param_ids);
    bool kwp2000_start_session(uint8_t session_type);
    bool kwp2000_security_access(uint16_t key);

    // CAN ������ (����� ELM327)
    bool initialize_can();
    bool set_can_baud_rate(uint32_t baud_rate);
    bool send_can_message(uint32_t id, const std::vector<uint8_t>& data, bool is_extended = false);
    std::vector<CANMessage> read_can_messages(int timeout_ms = 1000);
    bool set_can_filter(uint32_t id, uint32_t mask);
    bool start_can_monitoring(std::function<void(const CANMessage&)> callback);
    void stop_can_monitoring();
    bool test_can_bus();

    // UDS ������ (����� CAN)
    bool initialize_uds();
    UDSPacket send_uds_request(uint8_t service_id, const std::vector<uint8_t>& data = {});
    std::vector<uint8_t> uds_read_data_by_identifier(uint16_t did);
    bool uds_write_data_by_identifier(uint16_t did, const std::vector<uint8_t>& data);
    std::vector<DTCCode> uds_read_dtc(uint8_t dtc_type = 0x02);
    bool uds_clear_dtc(uint16_t group = 0xFFFF);
    bool uds_diagnostic_session_control(uint8_t session_type);
    uint8_t uds_ecu_reset(uint8_t reset_type);
    bool uds_security_access(uint8_t access_type, const std::vector<uint8_t>& key = {});
    bool tester_present();
    std::vector<uint8_t> uds_routine_control(uint8_t routine_id, const std::vector<uint8_t>& params = {});

    // Адресация UDS-сессии: по умолчанию функциональный запрос 0x7DF/0x7E8 (как раньше),
    // но чтение/запись памяти требует физического обращения к конкретному ЭБУ.
    void set_uds_target(uint32_t tx_id, uint32_t rx_id);
    void set_security_key_provider(std::shared_ptr<ISecurityKeyProvider> provider);

    // Полный цикл SecurityAccess: запрос seed -> вычисление ключа через провайдер -> отправка ключа.
    // Бросает std::runtime_error, если провайдер не настроен (см. NoKeyProvider).
    bool uds_unlock_security(uint8_t seed_level);

    // Реальная работа с памятью ЭБУ (0x23/0x34/0x36/0x37) — используется для
    // чтения/записи прошивки поверх CAN/UDS.
    std::vector<uint8_t> uds_read_memory_by_address(uint32_t address, uint16_t size, uint8_t addr_len_fmt = 0x24);
    bool uds_request_download(uint32_t address, uint32_t size, uint16_t& out_block_size, uint8_t addr_len_fmt = 0x24);
    bool uds_transfer_data(uint8_t block_seq, const std::vector<uint8_t>& chunk);
    bool uds_request_transfer_exit();

    // ���� ����������
    bool test_connection();

    // ����������
    void start_realtime_monitoring(const std::vector<std::string>& pids);
    void stop_realtime_monitoring();
    std::map<std::string, double> get_current_values();

    // ��������������� ������
    static std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
    static std::vector<uint8_t> hex_to_bytes(const std::string& hex);

private:
    std::unique_ptr<SerialPort> serial_port_;
    bool is_connected_;
    bool is_tcp_mode_;
    socket_t tcp_socket_;
    std::mutex socket_mutex_;

    std::map<std::string, OBDParameter> parameters_;
    std::map<uint16_t, KWP2000Parameter> kwp2000_parameters_;
    std::map<uint16_t, UDSParameter> uds_parameters_;

    std::thread monitor_thread_;
    bool monitoring_active_;
    std::function<void(const CANMessage&)> can_callback_;

    // CAN/UDS: адресация текущей сессии и кэш последнего "сырого" ответа
    // (ELM327 в режиме ATH0 возвращает голый hex без CAN ID — CANMessage-парсинг
    // для точечных UDS-запросов не подходит, используется только discover_can_nodes,
    // который сам включает ATH1 на время сканирования).
    uint32_t uds_tx_id_ = 0x7DF;
    uint32_t uds_rx_id_ = 0x7E8;
    std::string last_can_response_;
    std::shared_ptr<ISecurityKeyProvider> security_provider_ = std::make_shared<NoKeyProvider>();

    // ������� �������
    double parse_response(const std::string& response, const std::string& pid);
    std::vector<DTCCode> parse_dtc_response(const std::string& response);
    KWP2000Packet parse_kwp2000_response(const std::vector<uint8_t>& response);
    std::vector<DTCCode> parse_kwp2000_dtc(const std::vector<uint8_t>& data);
    std::vector<DTCCode> parse_uds_dtc(const std::vector<uint8_t>& data);
    std::vector<CANMessage> parse_can_messages(const std::string& response);

    // �������������
    void initialize_parameters();
    void initialize_kwp2000_parameters();
    void initialize_uds_parameters();

    // �����������
    static unsigned int hex_to_int(const std::string& hex);
    static std::string int_to_hex(unsigned int value);
    uint8_t calculate_kwp2000_checksum(const std::vector<uint8_t>& data);

    // ������� ������
    void cleanup_socket();
};

#endif