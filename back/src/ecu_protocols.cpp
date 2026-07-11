#include "../include/ecu_detector.h"
#include "../include/obd_connector.h"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
#include <set>
#include <iomanip>
#include <sstream>
#include <functional>
#include <fstream>
#include <cstring>
#include <bitset>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// ��� Windows ���������� INVALID_SOCKET ���� �� ����������
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SOCKET)(~0)
#endif
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

// ���������� ���������, ���� �� ���
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

// ����������� � ����������
ECUDETECTOR::ECUDETECTOR() :
    is_connected_(false),
    connection_handle_(nullptr),
#ifdef _WIN32
    tcp_socket_(INVALID_SOCKET),
#else
    tcp_socket_(-1),
#endif
    obd_connector_(std::make_unique<OBDConnector>()),
    cached_vin_(""),
    last_vin_read_(std::chrono::steady_clock::now()),
    last_live_read_(std::chrono::steady_clock::now()),
    last_dtc_read_(std::chrono::steady_clock::now()) {
}

ECUDETECTOR::~ECUDETECTOR() {
    disconnect();
#ifdef _WIN32
    if (tcp_socket_ != INVALID_SOCKET) {
        closesocket(tcp_socket_);
        WSACleanup();
    }
#else
    if (tcp_socket_ >= 0) {
        close(tcp_socket_);
    }
#endif
}

// ==================== TCP ������ ====================

bool ECUDETECTOR::connect_tcp(const std::string& host, int port) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }

    tcp_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket_ == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return false;
    }
#else
    tcp_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket_ < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return false;
    }
#endif

    // ������������� ������� �� ����������
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

#ifdef _WIN32
    setsockopt(tcp_socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(tcp_socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    setsockopt(tcp_socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(tcp_socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address" << std::endl;
#ifdef _WIN32
        closesocket(tcp_socket_);
        WSACleanup();
#else
        close(tcp_socket_);
#endif
        tcp_socket_ = INVALID_SOCKET;
        return false;
    }

#ifdef _WIN32
    if (connect(tcp_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(tcp_socket_);
        WSACleanup();
        tcp_socket_ = INVALID_SOCKET;
        return false;
    }
#else
    if (connect(tcp_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(tcp_socket_);
        tcp_socket_ = -1;
        return false;
    }
#endif

    std::cout << "TCP подключение к " << host << ":" << port << " �������" << std::endl;
    return true;
}

bool ECUDETECTOR::send_tcp_command(const std::string& cmd, std::string& response, int timeout_ms) {
#ifdef _WIN32
    if (tcp_socket_ == INVALID_SOCKET) return false;
#else
    if (tcp_socket_ < 0) return false;
#endif

    std::string cmd_with_term = cmd + "\r";
#ifdef _WIN32
    if (send(tcp_socket_, cmd_with_term.c_str(), (int)cmd_with_term.length(), 0) == SOCKET_ERROR) {
        return false;
    }
#else
    if (write(tcp_socket_, cmd_with_term.c_str(), cmd_with_term.length()) < 0) {
        return false;
    }
#endif

    char buffer[4096];
    auto start = std::chrono::steady_clock::now();
    response.clear();

    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count() < timeout_ms) {

#ifdef _WIN32
        int bytes = recv(tcp_socket_, buffer, sizeof(buffer) - 1, 0);
        if (bytes == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue;
            break;
        }
#else
        int bytes = read(tcp_socket_, buffer, sizeof(buffer) - 1);
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
#endif
        if (bytes > 0) {
            buffer[bytes] = '\0';
            response += buffer;

            // ���� ����������� ELM327
            if (response.find('>') != std::string::npos) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return !response.empty();
}

bool ECUDETECTOR::try_obd2_tcp_connection(const std::string& host, int port) {
    if (!connect_tcp(host, port)) return false;

    // ���������� ATZ ��� ������
    std::string response;
    if (!send_tcp_command("ATZ", response, 3000)) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // ��������� ������� ELM327
    if (!send_tcp_command("ATI", response, 2000) || response.empty()) {
        return false;
    }

    std::cout << "Тестируем ELM327 адаптер через TCP: " << response << std::endl;

    // ��������� ���
    send_tcp_command("ATE0", response);

    return true;
}

// ==================== ������������ ������ ====================

std::vector<PortInfo> ECUDETECTOR::scan_all_ports() {
    std::vector<PortInfo> all_ports;

    // ��������� �������� �����
    auto serial_ports = scan_serial_ports();
    all_ports.insert(all_ports.end(), serial_ports.begin(), serial_ports.end());

    // Сканируем только локальные TCP-адреса (удалённые IP убраны — они вызывают зависание)
    std::vector<std::pair<std::string, int>> common_tcp_ports = {
        {"127.0.0.1", 35000},   // ELM327 симулятор / локальный адаптер
        {"127.0.0.1", 23}       // Bluetooth ELM327 через COM2TCP
    };

    auto tcp_ports = scan_tcp_ports(common_tcp_ports);
    all_ports.insert(all_ports.end(), tcp_ports.begin(), tcp_ports.end());

    // ��������� Bluetooth �����
    auto bt_ports = scan_bluetooth_ports();
    all_ports.insert(all_ports.end(), bt_ports.begin(), bt_ports.end());

    return all_ports;
}

std::vector<PortInfo> ECUDETECTOR::scan_serial_ports() {
    std::vector<PortInfo> ports;

#ifdef _WIN32
    for (int i = 1; i <= 256; i++) {
        std::string port_name = "COM" + std::to_string(i);
        HANDLE hPort = CreateFileA(port_name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPort != INVALID_HANDLE_VALUE) {
            PortInfo info;
            info.name = port_name;
            info.type = "serial";
            info.description = "Serial port " + port_name;
            info.is_available = true;
            info.device_path = port_name;
            info.baud_rate = 115200;
            ports.push_back(info);
            CloseHandle(hPort);
        }
    }
#else
    std::vector<std::string> serial_devices = { "/dev/ttyUSB", "/dev/ttyACM", "/dev/ttyS" };
    for (const auto& base : serial_devices) {
        for (int i = 0; i < 10; i++) {
            std::string port_name = base + std::to_string(i);
            if (access(port_name.c_str(), F_OK) == 0) {
                PortInfo info;
                info.name = port_name;
                info.type = "serial";
                info.description = "Serial port " + port_name;
                info.is_available = true;
                info.device_path = port_name;
                info.baud_rate = 115200;
                ports.push_back(info);
            }
        }
    }
#endif

    return ports;
}

std::vector<PortInfo> ECUDETECTOR::scan_tcp_ports(const std::vector<std::pair<std::string, int>>& common_ports) {
    std::vector<PortInfo> ports;

    for (const auto& [host, port] : common_ports) {
        // ������� ��������� ����� ��� ��������
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        SOCKET test_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (test_sock == INVALID_SOCKET) continue;

        u_long mode = 1;
        ioctlsocket(test_sock, FIONBIO, &mode);
#else
        int test_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (test_sock < 0) continue;
#endif

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

#ifdef _WIN32
        connect(test_sock, (struct sockaddr*)&addr, sizeof(addr));
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(test_sock, &fdset);
        struct timeval tv = { 0, 200000 }; // 200ms таймаут

        if (select(test_sock + 1, NULL, &fdset, NULL, &tv) == 1) {
            int so_error;
            int len = sizeof(so_error);
            getsockopt(test_sock, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len);
            if (so_error == 0) {
                // ���� ������
                PortInfo info;
                info.name = host + ":" + std::to_string(port);
                info.type = "tcp";
                info.description = "TCP ELM327 emulator at " + host + ":" + std::to_string(port);
                info.is_available = true;
                info.host = host;
                info.port = port;
                ports.push_back(info);
            }
        }
        closesocket(test_sock);
#else
        int res = connect(test_sock, (struct sockaddr*)&addr, sizeof(addr));
        if (res == 0 || errno == EINPROGRESS) {
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(test_sock, &fdset);
            struct timeval tv = { 1, 0 };

            if (select(test_sock + 1, NULL, &fdset, NULL, &tv) == 1) {
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(test_sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0) {
                    PortInfo info;
                    info.name = host + ":" + std::to_string(port);
                    info.type = "tcp";
                    info.description = "TCP ELM327 emulator at " + host + ":" + std::to_string(port);
                    info.is_available = true;
                    info.host = host;
                    info.port = port;
                    ports.push_back(info);
                }
            }
        }
        close(test_sock);
#endif

#ifdef _WIN32
        WSACleanup();
#endif
    }

    return ports;
}

std::vector<PortInfo> ECUDETECTOR::scan_bluetooth_ports() {
    std::vector<PortInfo> ports;

#ifdef _WIN32
    // �� Windows Bluetooth COM ����� ������ ������������ ��� COM �����
    // ��� ��� ����� ������� � scan_serial_ports
#else
    // �� Linux ��������� RFCOMM ����������
    for (int i = 0; i < 10; i++) {
        std::string port_name = "/dev/rfcomm" + std::to_string(i);
        if (access(port_name.c_str(), F_OK) == 0) {
            PortInfo info;
            info.name = port_name;
            info.type = "bluetooth";
            info.description = "Bluetooth RFCOMM device " + port_name;
            info.is_available = true;
            info.device_path = port_name;
            info.baud_rate = 115200;
            ports.push_back(info);
        }
    }
#endif

    return ports;
}

// ==================== ������ ����������� ====================

ECUConnection ECUDETECTOR::connect_to_port(const PortInfo& port_info) {
    ECUConnection connection;
    connection.port = port_info.name;
    connection.port_type = port_info.type;
    connection.is_connected = false;

    std::cout << "Попытка подключения к " << port_info.type << " порту " << port_info.name << std::endl;

    bool connected = false;

    if (port_info.type == "serial" || port_info.type == "bluetooth") {
        // ELM327 adapters use different default baud rates depending on firmware.
        // Try common rates: 38400 is the most common default, then others as fallback.
        std::vector<unsigned int> baud_rates = {38400, 9600, 115200, 57600, 19200, 4800};
        // If a specific baud rate was requested (not the generic default 115200), try it first
        if (port_info.baud_rate != 115200) {
            baud_rates.insert(baud_rates.begin(), port_info.baud_rate);
        }
        for (unsigned int baud : baud_rates) {
            std::cout << "Попытка подключения на " << baud << " бод..." << std::endl;
            if (obd_connector_->connect(port_info.name, baud)) {
                connected = true;
                std::cout << "Подключено на скорости " << baud << " бод" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    else if (port_info.type == "tcp") {
        connected = obd_connector_->connect_tcp(port_info.host, port_info.port);

        if (connected) {
            std::cout << "TCP подключение установлено, старт адаптера" << std::endl;

            // ���� ����� �� �������������
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // ���������, ��� ���������� ������������� ���� ����� OBDConnector
            std::string test_response = obd_connector_->send_at_command("I");
            if (test_response.empty()) {
                std::cout << "ELM327 не отвечает после подключения" << std::endl;
                obd_connector_->disconnect();
                connected = false;
            }
            else {
                std::cout << "ELM327 ответил: " << test_response << std::endl;
            }
        }
    }

    if (connected) {
        current_port_ = port_info.name;
        current_port_type_ = port_info.type;
        is_connected_ = true;

        // ���������� �������� (��� �������� ����������!)
        std::string protocol = detect_protocol(current_port_);
        connection.protocol = protocol;
        current_protocol_ = protocol;

        // �������� ������� �����
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        connection.connection_time = std::ctime(&now_time_t);
        connection.connection_time.pop_back(); // ������� ������� ������

        // Читаем данные и кэшируем VIN сразу, чтобы check_connection() не перечитывал его заново
        auto connect_time = std::chrono::steady_clock::now();
        if (protocol == "OBD-II") {
            connection.vin = obd_connector_->read_vin();
            cached_vin_ = connection.vin;
            last_vin_read_ = connect_time;
            connection.ecu_type = "OBD-II ECU";
            connection.supported_pids = obd_connector_->get_supported_pids();
            connection.signal_quality = 95; // примерно
        }
        else if (protocol == "KWP2000") {
            connection.vin = obd_connector_->read_kwp2000_vin();
            cached_vin_ = connection.vin;
            last_vin_read_ = connect_time;
            connection.ecu_type = "KWP2000 ECU";
            connection.signal_quality = 85;
        }
        else if (protocol == "CAN") {
            connection.ecu_type = "CAN Bus";
            connection.signal_quality = 90;
        }
        else if (protocol == "UDS") {
            connection.vin = obd_connector_->read_kwp2000_vin();
            cached_vin_ = connection.vin;
            last_vin_read_ = connect_time;
            connection.ecu_type = "UDS ECU";
            connection.signal_quality = 88;
        }

        std::cout << "Успешное подключение через " << protocol << std::endl;
        connection.is_connected = true;
    }

    return connection;
}

// ��� �������� �������������
ECUConnection ECUDETECTOR::connect_to_ecu(const std::string& port) {
    PortInfo info;
    info.name = port;
    info.type = "serial"; // �� ���������
    info.baud_rate = 115200;
    return connect_to_port(info);
}

// ������������ ������ (������ ������ ��� �������� �������������)
std::vector<std::string> ECUDETECTOR::scan_ports() {
    std::vector<std::string> ports;

#ifdef _WIN32
    for (int i = 1; i <= 256; i++) {
        std::string port_name = "COM" + std::to_string(i);
        HANDLE hPorts = CreateFileA(port_name.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPorts != INVALID_HANDLE_VALUE) {
            ports.push_back(port_name);
            CloseHandle(hPorts);
        }
    }
#else
    // Linux: ��������� /dev/ttyUSB* � /dev/ttyACM*
    for (int i = 0; i < 10; i++) {
        std::string port = "/dev/ttyUSB" + std::to_string(i);
        if (access(port.c_str(), F_OK) == 0) {
            ports.push_back(port);
        }
    }

    for (int i = 0; i < 10; i++) {
        std::string port = "/dev/ttyACM" + std::to_string(i);
        if (access(port.c_str(), F_OK) == 0) {
            ports.push_back(port);
        }
    }
#endif

    // ��������� ����������� CAN ���������� ��� Linux
    ports.push_back("vcan0");
    ports.push_back("can0");
    ports.push_back("can1");

    return ports;
}

// ����������� ���������
std::string ECUDETECTOR::detect_protocol(const std::string& port) {
    std::cout << "Определение протокола на порту " << port << "..." << std::endl;

    if (try_obd2_connection(port)) {
        std::cout << "Проверяем протокол OBD-II" << std::endl;
        return "OBD-II";
    }

    if (try_kwp2000_connection(port)) {
        std::cout << "Проверяем протокол KWP2000" << std::endl;
        return "KWP2000";
    }

    if (try_can_connection(port)) {
        std::cout << "Проверяем протокол CAN" << std::endl;
        return "CAN";
    }

    if (try_uds_connection(port)) {
        std::cout << "Проверяем протокол UDS" << std::endl;
        return "UDS";
    }

    return "UNKNOWN";
}

// ������� OBD-II �����������
bool ECUDETECTOR::try_obd2_connection(const std::string& port) {
    (void)port;
    if (obd_connector_ && obd_connector_->is_connected()) {
        std::vector<int> protocols = { 0, 1, 2, 3, 4, 5, 6, 7 };

        for (int proto : protocols) {
            obd_connector_->set_protocol(proto);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            std::string response = obd_connector_->send_obd_command("0100");
            if (!response.empty() && response.find("NODATA") == std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// ������� KWP2000 �����������
bool ECUDETECTOR::try_kwp2000_connection(const std::string& port) {
    (void)port;
    if (obd_connector_ && obd_connector_->is_connected()) {
        std::vector<int> kwp_protocols = { 3, 4, 5, 10 };

        for (int proto : kwp_protocols) {
            obd_connector_->set_protocol(proto);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            if (obd_connector_->initialize_kwp2000()) {
                return true;
            }
        }
    }
    return false;
}

// ������� CAN �����������
bool ECUDETECTOR::try_can_connection(const std::string& port) {
    std::cout << "Проверка CAN подключения на " << port << "..." << std::endl;

    if (obd_connector_ && obd_connector_->is_connected()) {
        if (obd_connector_->initialize_can()) {
            auto nodes = discover_can_nodes();
            if (!nodes.empty()) {
                std::cout << "Обнаружены CAN узлы: ";
                for (const auto& node : nodes) {
                    std::cout << "0x" << std::hex << node.first << " ";
                }
                std::cout << std::dec << std::endl;
                return true;
            }
        }
    }
    return false;
}

// ������� UDS �����������
bool ECUDETECTOR::try_uds_connection(const std::string& port) {
    std::cout << "Проверка UDS подключения на " << port << "..." << std::endl;

    if (obd_connector_ && obd_connector_->is_connected()) {
        if (obd_connector_->initialize_uds()) {
            if (uds_discover_nodes()) {
                std::cout << "UDS протокол обнаружен" << std::endl;
                return true;
            }
        }
    }
    return false;
}

// ==================== CAN ������ ====================

bool ECUDETECTOR::initialize_can(const std::string& interface) {
    std::cout << "Инициализация CAN интерфейса: " << interface << std::endl;

    if (!obd_connector_ || !obd_connector_->is_connected()) {
        std::cerr << "Ошибка: адаптер не подключён" << std::endl;
        return false;
    }

    current_port_ = interface;
    bool can_initialized = false;

    // ������� 1: ���������������
    std::cout << "Попытка 1: автоматическое подключение CAN..." << std::endl;
    if (obd_connector_->set_protocol(0)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::string response = obd_connector_->send_obd_command("0100");
        if (!response.empty() && response != "NODATA") {
            std::cout << "CAN подключён к интерфейсу" << std::endl;
            can_initialized = true;
        }
    }

    // ������� 2: CAN 11/500
    if (!can_initialized) {
        std::cout << "Попытка 2: CAN 11-bit, 500 kbps..." << std::endl;
        if (obd_connector_->set_protocol(6)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::string response = obd_connector_->send_obd_command("0100");
            if (!response.empty() && response != "NODATA") {
                std::cout << "CAN 11/500 успешно инициализирован" << std::endl;
                can_initialized = true;
            }
        }
    }

    // ������� 3: CAN 11/250
    if (!can_initialized) {
        std::cout << "Попытка 3: CAN 11-bit, 250 kbps..." << std::endl;
        if (obd_connector_->set_protocol(7)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::string response = obd_connector_->send_obd_command("0100");
            if (!response.empty() && response != "NODATA") {
                std::cout << "CAN 11/250 успешно инициализирован" << std::endl;
                can_initialized = true;
            }
        }
    }

    // ������� 4: ������ ���������
    if (!can_initialized) {
        std::cout << "Попытка 4: ручная настройка CAN..." << std::endl;

        obd_connector_->send_at_command("Z");
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        obd_connector_->send_at_command("E0");
        obd_connector_->send_at_command("H1");
        obd_connector_->send_at_command("SP 6");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        obd_connector_->send_at_command("CSM 1");
        obd_connector_->send_at_command("CRA 7E8");
        obd_connector_->send_at_command("SH 7DF");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::string response = obd_connector_->send_obd_command("0100");
        if (!response.empty() && response != "NODATA") {
            std::cout << "CAN адаптер настроен успешно" << std::endl;
            can_initialized = true;
        }
    }

    if (can_initialized) {
        std::cout << "CAN инициализация успешно завершена" << std::endl;
        std::string can_info = obd_connector_->send_at_command("DP");
        std::cout << "Текущий протокол: " << can_info << std::endl;
        return true;
    }
    else {
        std::cerr << "Не удалось инициализировать CAN" << std::endl;
        return false;
    }
}

std::map<uint32_t, std::vector<uint8_t>> ECUDETECTOR::discover_can_nodes() {
    std::map<uint32_t, std::vector<uint8_t>> nodes;

    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return nodes;
    }

    std::cout << "Сканирование CAN порта..." << std::endl;

    std::vector<uint32_t> diagnostic_ids = {
        0x7E0, 0x7E1, 0x7E2, 0x7E3, 0x7E4, 0x7E5, 0x7E6, 0x7E7,
        0x7E8, 0x7E9, 0x7EA, 0x7EB, 0x7EC, 0x7ED, 0x7EE, 0x7EF
    };

    // На время сканирования нужны заголовки (ATH1) — иначе в ответе не видно,
    // какой ID ответил. В остальном коде (точечные UDS-запросы) заголовки
    // выключены (ATH0, initialize_adapter()) — обязательно возвращаем как было.
    obd_connector_->send_at_command("H1");

    for (uint32_t id : diagnostic_ids) {
        std::vector<uint8_t> request = { 0x01, 0x00 };

        if (obd_connector_->send_can_message(id, request)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            auto messages = obd_connector_->read_can_messages(100);

            for (const auto& msg : messages) {
                if (msg.id == id + 8 || msg.id == id - 8) {
                    nodes[id] = msg.data;
                    std::cout << "  Найден узел: 0x" << std::hex << id << std::dec << std::endl;
                    break;
                }
            }
        }
    }

    obd_connector_->send_at_command("H0");

    std::cout << "Найдено узлов: " << nodes.size() << std::endl;
    return nodes;
}

bool ECUDETECTOR::send_can_diagnostic_request(uint32_t id, const std::vector<uint8_t>& data) {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return false;
    }
    return obd_connector_->send_can_message(id, data);
}

std::vector<CANMessage> ECUDETECTOR::monitor_can_bus(int duration_ms) {
    std::vector<CANMessage> all_messages;

    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return all_messages;
    }

    std::cout << "Мониторинг CAN шины " << duration_ms << " мс..." << std::endl;

    obd_connector_->send_at_command("CSM 1");
    obd_connector_->send_at_command("ATMA");

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count() < duration_ms) {

        auto messages = obd_connector_->read_can_messages(100);
        auto now = std::chrono::system_clock::now();

        for (auto& msg : messages) {
            msg.timestamp = now;
        }

        // ���������� ������� - ���������� push_back ������ insert � �����������
        for (const auto& msg : messages) {
            all_messages.push_back(msg);
        }

        int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
        std::cout << "\rПрогресс: " << (elapsed * 100 / duration_ms) << "%" << std::flush;
    }

    obd_connector_->send_at_command("CSM 0");
    std::cout << "\nПолучено сообщений: " << all_messages.size() << std::endl;

    return all_messages;
}

std::string ECUDETECTOR::identify_node_by_id(uint32_t id) {
    switch (id) {
    case 0x7E0: return "Engine ECU (Request)";
    case 0x7E8: return "Engine ECU (Response)";
    case 0x7E1: return "Transmission ECU (Request)";
    case 0x7E9: return "Transmission ECU (Response)";
    case 0x7E2: return "ABS ECU (Request)";
    case 0x7EA: return "ABS ECU (Response)";
    case 0x7E3: return "Airbag ECU (Request)";
    case 0x7EB: return "Airbag ECU (Response)";
    case 0x7E4: return "Instrument Cluster (Request)";
    case 0x7EC: return "Instrument Cluster (Response)";
    case 0x7E5: return "Climate Control (Request)";
    case 0x7ED: return "Climate Control (Response)";
    case 0x7E6: return "Immobilizer (Request)";
    case 0x7EE: return "Immobilizer (Response)";
    case 0x7E7: return "Auxiliary Systems (Request)";
    case 0x7EF: return "Auxiliary Systems (Response)";
    case 0x7DF: return "Broadcast Request";
    default: {
        std::stringstream ss;
        ss << "Unknown CAN Node (0x" << std::hex << id << std::dec << ")";
        return ss.str();
    }
    }
}

bool ECUDETECTOR::start_can_monitoring(std::function<void(const CANMessage&)> callback) {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return false;
    }
    return obd_connector_->start_can_monitoring(callback);
}

void ECUDETECTOR::stop_can_monitoring() {
    if (obd_connector_) {
        obd_connector_->stop_can_monitoring();
    }
}

std::vector<uint32_t> ECUDETECTOR::get_active_can_ids() {
    std::vector<uint32_t> ids;
    auto messages = monitor_can_bus(2000);

    std::set<uint32_t> unique_ids;
    for (const auto& msg : messages) {
        unique_ids.insert(msg.id);
    }

    ids.assign(unique_ids.begin(), unique_ids.end());
    std::sort(ids.begin(), ids.end());

    return ids;
}

std::map<uint32_t, std::string> ECUDETECTOR::identify_can_nodes() {
    std::map<uint32_t, std::string> identification;
    auto nodes = discover_can_nodes();

    for (const auto& node : nodes) {
        identification[node.first] = identify_node_by_id(node.first);
    }

    return identification;
}

bool ECUDETECTOR::set_can_filter(uint32_t id, uint32_t mask) {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return false;
    }
    return obd_connector_->set_can_filter(id, mask);
}

bool ECUDETECTOR::send_can_message(uint32_t id, const std::vector<uint8_t>& data, bool is_extended) {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return false;
    }

    if (data.size() > 8) {
        std::cerr << "Ошибка: CAN сообщение > 8 байт" << std::endl;
        return false;
    }

    return obd_connector_->send_can_message(id, data, is_extended);
}

std::vector<CANMessage> ECUDETECTOR::read_can_messages(int timeout_ms) {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return {};
    }
    return obd_connector_->read_can_messages(timeout_ms);
}

bool ECUDETECTOR::test_can_bus() {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return false;
    }

    std::cout << "Сканирование CAN шины..." << std::endl;
    auto nodes = discover_can_nodes();

    if (nodes.empty()) {
        std::cout << "CAN шина открыта, узлы не обнаружены" << std::endl;
        return false;
    }

    std::cout << "CAN шина запущена. Узлов: " << nodes.size() << std::endl;
    return true;
}

// ==================== UDS ������ ====================

bool ECUDETECTOR::initialize_uds(const std::string& interface) {
    (void)interface;
    if (!obd_connector_) return false;
    return obd_connector_->initialize_uds();
}

bool ECUDETECTOR::uds_discover_nodes() {
    if (!obd_connector_) return false;

    std::vector<uint16_t> test_dids = { 0xF190, 0xF180, 0x9020, 0x9010 };

    for (uint16_t did : test_dids) {
        auto data = obd_connector_->uds_read_data_by_identifier(did);
        if (!data.empty()) {
            std::string vin(data.begin(), data.end());
            if (vin.length() == 17) {
                std::cout << "Найден ECU с VIN: " << vin << std::endl;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return false;
}

void ECUDETECTOR::uds_set_target(uint32_t tx_id, uint32_t rx_id) {
    if (obd_connector_) {
        obd_connector_->set_uds_target(tx_id, rx_id);
    }
}

bool ECUDETECTOR::uds_enter_session(uint8_t session_type) {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return false;
    }
    return obd_connector_->uds_diagnostic_session_control(session_type);
}

std::vector<uint8_t> ECUDETECTOR::uds_request_seed(uint8_t access_level, bool& already_unlocked) {
    already_unlocked = false;
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return {};
    }

    auto response = obd_connector_->send_uds_request(0x27, { access_level });
    if (response.has_negative_response() || response.data.empty()) {
        return {};
    }

    // Позитивный ответ 0x67: [эхо access_level][seed...].
    std::vector<uint8_t> seed;
    if (response.data.size() > 1) {
        seed.assign(response.data.begin() + 1, response.data.end());
    }

    if (seed.empty() || std::all_of(seed.begin(), seed.end(), [](uint8_t b) { return b == 0; })) {
        already_unlocked = true;
        return {};
    }

    return seed;
}

bool ECUDETECTOR::uds_send_key(uint8_t access_level, const std::vector<uint8_t>& key) {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return false;
    }

    std::vector<uint8_t> request = { access_level };
    request.insert(request.end(), key.begin(), key.end());

    auto response = obd_connector_->send_uds_request(0x27, request);
    return !response.has_negative_response();
}

uint16_t ECUDETECTOR::uds_get_vin_did() {
    if (!obd_connector_) return 0;

    std::vector<uint16_t> possible_dids = { 0xF190, 0xF180, 0x9020, 0x9010, 0x1200, 0x1300 };

    for (uint16_t did : possible_dids) {
        auto data = obd_connector_->uds_read_data_by_identifier(did);
        if (!data.empty()) {
            std::string test_str(data.begin(), data.end());
            if (test_str.length() == 17 || test_str.length() > 10) {
                return did;
            }
        }
    }

    return 0;
}

std::map<uint16_t, std::string> ECUDETECTOR::get_supported_dids() {
    std::map<uint16_t, std::string> supported_dids;

    if (!obd_connector_) return supported_dids;

    std::vector<uint16_t> test_dids = {
        0xF180, 0xF190, 0xF1A0, 0xF1B0, 0xF1C0, 0xF1D0, 0xF1E0, 0xF1F0,
        0xF200, 0xF201, 0xF202, 0xF203, 0xF204, 0xF205, 0xF206, 0xF207, 0xF208
    };

    for (uint16_t did : test_dids) {
        auto data = obd_connector_->uds_read_data_by_identifier(did);
        if (!data.empty()) {
            supported_dids[did] = "Supported DID 0x" + OBDConnector::bytes_to_hex({ uint8_t(did >> 8), uint8_t(did & 0xFF) });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return supported_dids;
}

bool ECUDETECTOR::uds_ecu_reset(uint8_t reset_type) {
    if (!obd_connector_) return false;
    return obd_connector_->uds_ecu_reset(reset_type) != 0;
}

std::map<uint16_t, std::vector<DTCCode>> ECUDETECTOR::read_extended_dtc_info() {
    std::map<uint16_t, std::vector<DTCCode>> extended_dtc;

    if (!obd_connector_) return extended_dtc;

    auto dtc_list = obd_connector_->uds_read_dtc(0x02);

    for (const auto& dtc : dtc_list) {
        uint16_t dtc_key = 0;
        try {
            dtc_key = static_cast<uint16_t>(std::stoul(dtc.code.substr(1), nullptr, 16));
        }
        catch (...) {
            continue;
        }
        extended_dtc[dtc_key].push_back(dtc);
    }

    return extended_dtc;
}

bool ECUDETECTOR::uds_flash_ecu(const std::string& firmware_path) {
    // Полная автоматическая прошивка требует карты памяти ЭБУ (стартовый адрес
    // программируемой области, размер, контрольные суммы) — этих данных пока нет
    // ни в одном слое проекта. Используйте uds_write_memory() напрямую с явным
    // адресом, когда карта памяти конкретного ЭБУ будет подключена.
    (void)firmware_path;
    std::cerr << "uds_flash_ecu: не реализовано — нужен явный адрес программирования "
                 "(карта памяти ЭБУ пока не подключена). Используйте uds_write_memory()."
              << std::endl;
    return false;
}

std::vector<uint8_t> ECUDETECTOR::uds_read_memory(uint32_t address, uint32_t size) {
    if (!obd_connector_ || !obd_connector_->is_connected()) {
        return {};
    }

    std::vector<uint8_t> result;
    result.reserve(size);

    // Читаем блоками (addr_len_fmt=0x14: 4-байтовый адрес, 1-байтовый memorySize —
    // безопасный размер блока для большинства ELM327/CAN-соединений).
    const uint32_t CHUNK = 0xFF;
    uint32_t remaining = size;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint16_t chunk_size = static_cast<uint16_t>(std::min<uint32_t>(remaining, CHUNK));
        auto chunk = obd_connector_->uds_read_memory_by_address(address + offset, chunk_size, 0x14);
        if (chunk.empty()) {
            std::cerr << "uds_read_memory: обрыв чтения на смещении 0x"
                       << std::hex << offset << std::dec << std::endl;
            break;
        }
        result.insert(result.end(), chunk.begin(), chunk.end());
        offset += static_cast<uint32_t>(chunk.size());
        remaining -= static_cast<uint32_t>(chunk.size());

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return result;
}

bool ECUDETECTOR::uds_write_memory(uint32_t address, const std::vector<uint8_t>& data) {
    if (!obd_connector_ || !obd_connector_->is_connected() || data.empty()) {
        return false;
    }

    uint16_t block_size = 0;
    if (!obd_connector_->uds_request_download(address, static_cast<uint32_t>(data.size()), block_size, 0x14)) {
        std::cerr << "uds_write_memory: RequestDownload отклонён ЭБУ" << std::endl;
        return false;
    }

    uint8_t block_seq = 1;
    size_t offset = 0;
    while (offset < data.size()) {
        size_t chunk_len = std::min<size_t>(block_size, data.size() - offset);
        std::vector<uint8_t> chunk(data.begin() + offset, data.begin() + offset + chunk_len);

        if (!obd_connector_->uds_transfer_data(block_seq, chunk)) {
            std::cerr << "uds_write_memory: TransferData отклонён на блоке "
                       << static_cast<int>(block_seq) << std::endl;
            obd_connector_->uds_request_transfer_exit(); // пробуем корректно закрыть сессию
            return false;
        }

        offset += chunk_len;
        block_seq = static_cast<uint8_t>((block_seq % 0xFF) + 1); // 1..0xFF по кругу, как в ISO 14229

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!obd_connector_->uds_request_transfer_exit()) {
        std::cerr << "uds_write_memory: RequestTransferExit отклонён ЭБУ" << std::endl;
        return false;
    }

    return true;
}

// KWP2000 ������
bool ECUDETECTOR::initialize_kwp2000() {
    return obd_connector_ ? obd_connector_->initialize_kwp2000() : false;
}

bool ECUDETECTOR::kwp2000_handshake() {
    if (!obd_connector_) return false;
    return obd_connector_->kwp2000_start_session(0x81);
}

std::vector<uint8_t> ECUDETECTOR::kwp2000_send_command(const std::vector<uint8_t>& command) {
    return obd_connector_ ? obd_connector_->send_kwp2000_command(command) : std::vector<uint8_t>();
}

uint16_t ECUDETECTOR::kwp2000_calculate_checksum(const std::vector<uint8_t>& data) {
    uint16_t sum = 0;
    for (uint8_t byte : data) {
        sum += byte;
    }
    return sum & 0xFF;
}

bool ECUDETECTOR::kwp2000_security_access(uint16_t key) {
    return obd_connector_ ? obd_connector_->kwp2000_security_access(key) : false;
}

bool ECUDETECTOR::kwp2000_write_data(uint16_t param_id, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> command;
    command.push_back(0x04);
    command.push_back(0x2E);
    command.push_back(param_id >> 8);
    command.push_back(param_id & 0xFF);
    command.insert(command.end(), data.begin(), data.end());

    auto response = kwp2000_send_command(command);
    return response.size() >= 2 && response[1] == 0x6E;
}

// ==================== �������� ������ ����������� � ������������ ====================
ECUConnection ECUDETECTOR::check_connection() {
    ECUConnection status;

    // ��������� ���������� ����������
    if (is_connected_ && obd_connector_ && obd_connector_->is_connected()) {
        // ���������� keep-alive ������� ��� ����������� ����������
        static auto last_keep_alive = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_keep_alive).count() >= 5) {
            // ������ 5 ������ ���������� ������ ������� ��� ����������� ����������
            if (current_protocol_ == "OBD-II") {
                obd_connector_->send_at_command("ATI");
            }
            last_keep_alive = now;
        }

        status.is_connected = true;
        status.protocol = current_protocol_;
        status.port = current_port_;
        status.port_type = current_port_type_;

        // ���������� ������������ VIN ���� �� ����
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_vin_read_);

        if (cached_vin_.empty() || duration.count() > CACHE_DURATION_MS * 10) {
            if (current_protocol_ == "OBD-II") {
                cached_vin_ = obd_connector_->read_vin();
                status.vin = cached_vin_;
                status.ecu_type = "OBD-II ECU";
                status.supported_pids = obd_connector_->get_supported_pids();
            }
            else if (current_protocol_ == "KWP2000") {
                cached_vin_ = obd_connector_->read_kwp2000_vin();
                status.vin = cached_vin_;
                status.ecu_type = "KWP2000 ECU";
            }
            else if (current_protocol_ == "CAN") {
                status.ecu_type = "CAN Bus";
                auto nodes = discover_can_nodes();
                status.diagnostic_data["nodes"] = std::to_string(nodes.size());
            }
            else if (current_protocol_ == "UDS") {
                cached_vin_ = obd_connector_->read_kwp2000_vin();
                status.vin = cached_vin_;
                status.ecu_type = "UDS ECU";
            }
            last_vin_read_ = now;
        }
        else {
            status.vin = cached_vin_;
            if (current_protocol_ == "OBD-II") {
                status.ecu_type = "OBD-II ECU";
                // supported_pids уже получены при подключении, не запрашиваем повторно
            }
            else if (current_protocol_ == "KWP2000") {
                status.ecu_type = "KWP2000 ECU";
            }
            else if (current_protocol_ == "CAN") {
                status.ecu_type = "CAN Bus";
            }
            else if (current_protocol_ == "UDS") {
                status.ecu_type = "UDS ECU";
            }
        }

        status.signal_quality = 95;
    }
    else {
        status.is_connected = false;
        status.protocol = "NONE";
        status.port = "NONE";
        status.port_type = "NONE";
        status.signal_quality = 0;
        is_connected_ = false;
        clear_cache();
    }

    return status;
}

std::map<std::string, std::string> ECUDETECTOR::read_ecu_info() {
    std::map<std::string, std::string> info;

    if (is_connected_ && obd_connector_ && obd_connector_->is_connected()) {
        if (current_protocol_ == "OBD-II") {
            info["vin"] = cached_vin_.empty() ? obd_connector_->read_vin() : cached_vin_;
            info["ecu_type"] = "OBD-II ECU";

            // Mode 09 PID 04 - Calibration ID
            {
                std::string r09 = obd_connector_->send_obd_command("0904");
                std::string calid;
                if (r09.size() > 6) {
                    for (size_t i = 6; i + 1 < r09.size(); i += 3) {
                        try { int v = std::stoi(r09.substr(i,2),nullptr,16);
                              if (v>=0x20&&v<=0x7E) calid+=(char)v; } catch(...){}
                    }
                }
                if (!calid.empty()) info["calibration_id"] = calid;
            }

            // Mode 09 PID 0A - ECU Name
            {
                std::string r0a = obd_connector_->send_obd_command("090A");
                std::string ecu_name;
                if (r0a.size() > 6) {
                    for (size_t i = 6; i + 1 < r0a.size(); i += 3) {
                        try { int v = std::stoi(r0a.substr(i,2),nullptr,16);
                              if (v>=0x20&&v<=0x7E) ecu_name+=(char)v; } catch(...){}
                    }
                }
                if (!ecu_name.empty()) info["ecu_name"] = ecu_name;
            }

            // ������ DTC ������ ���� �����, ���������� ���
            auto dtcs = read_real_errors();
            info["dtc_count"] = std::to_string(dtcs.size());

            // Live ������ ������ ����� ��������� ����� � ������������
            auto live = read_live_data();
            info["current_rpm"] = std::to_string(live["010C"]);
            info["current_speed"] = std::to_string(live["010D"]);
        }
        else if (current_protocol_ == "KWP2000") {
            info["vin"] = cached_vin_.empty() ? obd_connector_->read_kwp2000_vin() : cached_vin_;
            info["ecu_type"] = "KWP2000 ECU";

            auto dtcs = read_real_errors();
            info["dtc_count"] = std::to_string(dtcs.size());

            std::vector<uint16_t> param_ids = { 0x0101, 0x0102, 0x0103, 0x0105 };
            auto params = obd_connector_->read_kwp2000_parameters(param_ids);
            for (const auto& param : params) {
                info["param_" + std::to_string(param.first)] = std::to_string(param.second);
            }
        }
        else if (current_protocol_ == "CAN") {
            info["ecu_type"] = "CAN Bus";
            info["nodes"] = std::to_string(discover_can_nodes().size());
        }
        else if (current_protocol_ == "UDS") {
            info["vin"] = cached_vin_.empty() ? obd_connector_->read_kwp2000_vin() : cached_vin_;
            info["ecu_type"] = "UDS ECU";
            info["dids"] = std::to_string(get_supported_dids().size());
        }
    }
    else {
        info["ecu_type"] = "Not connected";
        info["vin"] = "Unknown";
    }

    return info;
}

std::map<std::string, double> ECUDETECTOR::read_live_data() {
    std::map<std::string, double> data;

    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_live_read_);

    if (!is_connected_ || !obd_connector_ || !obd_connector_->is_connected()) {
        return data;
    }

    // ���� ������ � ���� ������, ���������� ��
    if (!cached_live_data_.empty() && duration.count() < CACHE_DURATION_MS) {
        return cached_live_data_;
    }

    // Читаем данные в зависимости от протокола
    if (current_protocol_ == "OBD-II") {
        auto raw = obd_connector_->read_multiple_parameters({
            "010C", "010D", "0105", "0111", "0110", "010F", "010A"
        });
        // Маппинг PID-кодов в имена, которые ожидает фронтенд
        if (raw.count("010C")) data["rpm"]          = raw["010C"];
        if (raw.count("010D")) data["speed"]         = raw["010D"];
        if (raw.count("0105")) data["coolant"]       = raw["0105"];
        if (raw.count("0111")) data["throttle"]      = raw["0111"];
        if (raw.count("0110")) data["maf"]           = raw["0110"];
        if (raw.count("010F")) data["intake"]        = raw["010F"];
        if (raw.count("010A")) data["fuel_pressure"] = raw["010A"];

        // Напряжение бортовой сети через AT-команду
        std::string volt_str = obd_connector_->send_at_command("RV");
        if (volt_str.size() > 1 && volt_str.back() == 'V') {
            try {
                data["voltage"] = std::stod(volt_str.substr(0, volt_str.size() - 1));
            } catch (...) {}
        }
    }
    else if (current_protocol_ == "KWP2000") {
        std::vector<uint16_t> param_ids = { 0x0101, 0x0102, 0x0103, 0x0104, 0x0105 };
        auto kwp_data = obd_connector_->read_kwp2000_parameters(param_ids);
        for (const auto& item : kwp_data) {
            switch (item.first) {
                case 0x0101: data["rpm"]      = item.second; break;
                case 0x0102: data["speed"]    = item.second; break;
                case 0x0103: data["coolant"]  = item.second; break;
                case 0x0104: data["intake"]   = item.second; break;
                case 0x0105: data["throttle"] = item.second; break;
                default: break;
            }
        }
    }

    cached_live_data_ = data;
    last_live_read_ = now;

    return data;
}

std::vector<DTCCode> ECUDETECTOR::read_real_errors() {
    std::vector<DTCCode> errors;

    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_dtc_read_);

    if (!is_connected_ || !obd_connector_ || !obd_connector_->is_connected()) {
        return errors;
    }

    // ���� DTC � ���� ������, ���������� ��
    if (!cached_dtc_codes_.empty() && duration.count() < CACHE_DURATION_MS * 5) { // DTC �������� �� 5 ������
        return cached_dtc_codes_;
    }

    // ����� ������ �����
    if (current_protocol_ == "OBD-II") {
        errors = obd_connector_->read_dtc_codes();
    }
    else if (current_protocol_ == "KWP2000") {
        errors = obd_connector_->read_kwp2000_dtc();
    }

    cached_dtc_codes_ = errors;
    last_dtc_read_ = now;

    return errors;
}

bool ECUDETECTOR::clear_errors() {
    bool result = false;

    if (is_connected_ && obd_connector_ && obd_connector_->is_connected()) {
        if (current_protocol_ == "OBD-II") {
            result = obd_connector_->clear_dtc_codes();
        }
        else if (current_protocol_ == "KWP2000") {
            result = obd_connector_->clear_kwp2000_dtc();
        }

        // ������� ��� DTC ����� ��������
        if (result) {
            cached_dtc_codes_.clear();
            last_dtc_read_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(CACHE_DURATION_MS * 10);
        }
    }

    return result;
}

void ECUDETECTOR::disconnect() {
    if (is_connected_) {
        std::cout << "Отправляем на ЭБУ..." << std::endl;

        if (obd_connector_) {
            obd_connector_->disconnect();
        }

#ifdef _WIN32
        if (tcp_socket_ != INVALID_SOCKET) {
            closesocket(tcp_socket_);
            tcp_socket_ = INVALID_SOCKET;
        }
#else
        if (tcp_socket_ >= 0) {
            close(tcp_socket_);
            tcp_socket_ = -1;
        }
#endif

        is_connected_ = false;
        current_port_.clear();
        current_port_type_.clear();
        current_protocol_.clear();
        connection_handle_ = nullptr;

        // ������� ���
        clear_cache();
    }
}

// ������ ��� ��������� ���������� � �������������� ����������
std::vector<std::string> ECUDETECTOR::get_supported_protocols() const {
    return { "OBD-II", "KWP2000", "CAN", "UDS" };
}

std::string ECUDETECTOR::get_protocol_description(const std::string& protocol) const {
    if (protocol == "OBD-II") {
        return "On-Board Diagnostics II - ����������� �������� ��� ����������� ����������� (ISO 15765-4, SAE J1979)";
    }
    else if (protocol == "KWP2000") {
        return "Keyword Protocol 2000 - �������� ����������� ����� K-Line (ISO 14230)";
    }
    else if (protocol == "CAN") {
        return "Controller Area Network - ���������������� ���� ������ ��� ����� ��� (ISO 11898)";
    }
    else if (protocol == "UDS") {
        return "Unified Diagnostic Services - ��������������� �������� ����������� (ISO 14229)";
    }
    return "����������� ��������";
}