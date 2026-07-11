#include "../include/obd_connector.h"

#include <iostream>

#include <sstream>

#include <iomanip>

#include <thread>

#include <chrono>

#include <algorithm>

#include <cstring>

#include <bitset>

#include <cstdlib>



#ifdef _WIN32

#include <windows.h>

#else

#include <fcntl.h>

#include <errno.h>

#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <netdb.h>

#endif



OBDConnector::OBDConnector() :

    is_connected_(false),

    is_tcp_mode_(false),

    tcp_socket_(INVALID_SOCKET),

    monitoring_active_(false) {

    serial_port_ = std::make_unique<SerialPort>();

    initialize_parameters();

    initialize_kwp2000_parameters();

    initialize_uds_parameters();

}



OBDConnector::~OBDConnector() {

    stop_can_monitoring();

    disconnect();

    cleanup_socket();

}



void OBDConnector::cleanup_socket() {

    // ������ lock_guard - ������� ������ ���� �������� � ���������� ������

    if (tcp_socket_ != INVALID_SOCKET) {

#ifdef _WIN32

        closesocket(tcp_socket_);

        WSACleanup();

#else

        ::close(tcp_socket_);

#endif

        tcp_socket_ = INVALID_SOCKET;

    }

}



void OBDConnector::set_tcp_socket(socket_t sock) {

    std::lock_guard<std::mutex> lock(socket_mutex_);

    cleanup_socket(); // cleanup_socket ������ �� �������� ��������� �������

    tcp_socket_ = sock;

    is_tcp_mode_ = (sock != INVALID_SOCKET);

}



socket_t OBDConnector::get_tcp_socket() const {

    return tcp_socket_;

}



bool OBDConnector::is_tcp_mode() const {

    return is_tcp_mode_;

}



// back/src/obd_connector.cpp - ������������ ����� connect_tcp



bool OBDConnector::connect_tcp(const std::string& host, int port) {

    // ��������� socket_mutex_ ������ ��� ���������/��������� ������.

    // initialize_adapter() ����������� ����� ��������, ������� ����

    // ������ ���� ������ ������ ��������.

    {

        std::lock_guard<std::mutex> lock(socket_mutex_);



        // ���� ��� ���� ����������, ��������� ������ �����

        if (tcp_socket_ != INVALID_SOCKET) {

            cleanup_socket();

        }



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



        // ������������� ����� ������ ��� keep-alive

        int keepalive = 1;

        setsockopt(tcp_socket_, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepalive, sizeof(keepalive));



#ifdef _WIN32

        DWORD timeout = 30000;

        setsockopt(tcp_socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        setsockopt(tcp_socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

#else

        struct timeval timeout;

        timeout.tv_sec = 30;

        timeout.tv_usec = 0;

        setsockopt(tcp_socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        setsockopt(tcp_socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

#endif



        struct sockaddr_in server_addr;

        memset(&server_addr, 0, sizeof(server_addr));

        server_addr.sin_family = AF_INET;

        server_addr.sin_port = htons(port);



        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {

            struct hostent* host_entry = gethostbyname(host.c_str());

            if (host_entry == nullptr) {

                std::cerr << "Failed to resolve hostname: " << host << std::endl;

                cleanup_socket();

                return false;

            }

            memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);

        }



        int connect_result = ::connect(tcp_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr));



#ifdef _WIN32

        if (connect_result == SOCKET_ERROR) {

            int error = WSAGetLastError();

            std::cerr << "Connection failed with error: " << error << std::endl;

            cleanup_socket();

            return false;

        }

#else

        if (connect_result < 0) {

            std::cerr << "Connection failed with error: " << strerror(errno) << std::endl;

            cleanup_socket();

            return false;

        }

#endif



        is_tcp_mode_ = true;

        is_connected_ = true;

        std::cout << "TCP подключение к " << host << ":" << port << " открыто" << std::endl;

    } // lock_guard ����������� ������, ����� initialize_adapter �� ������������



    // �������������� ELM327 �������

    bool init_result = initialize_adapter();



    if (!init_result) {

        std::cerr << "Не удалось инициализировать ELM327 адаптер" << std::endl;

        return false;

    }



    return true;

}



bool OBDConnector::connect(const std::string& port, unsigned int baud_rate) {

    std::cout << "Подключение к " << port << " со скоростью " << baud_rate << " бод..." << std::endl;



    if (serial_port_->open(port, baud_rate)) {

        is_tcp_mode_ = false;

        is_connected_ = true;



        if (!initialize_adapter()) {

            std::cout << "Не удалось инициализировать ELM327 адаптер" << std::endl;

            serial_port_->close();

            is_connected_ = false;

            return false;

        }



        std::cout << "ELM327 адаптер инициализирован успешно" << std::endl;

        return true;

    }



    std::cout << "Не удалось открыть порт " << port << std::endl;

    return false;

}



void OBDConnector::disconnect() {

    stop_can_monitoring();



    if (is_connected_) {

        // Сбрасываем ELM327 перед закрытием — иначе он буферизирует CAN данные

        // и при следующем подключении выдаёт тысячи байт мусора

        try {

            if (is_tcp_mode_) {

                send_tcp_command("ATZ", 1000);

            } else {

                serial_port_->write("ATZ\r");

                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                serial_port_->flush();

            }

        }

        catch (...) {

        }

    }



    if (is_connected_) {

        if (is_tcp_mode_) {

            std::lock_guard<std::mutex> lock(socket_mutex_);

            cleanup_socket();

        }

        else {

            serial_port_->close();

        }

        is_connected_ = false;

        is_tcp_mode_ = false;

        std::cout << "Отключение успешно" << std::endl;

    }

}



bool OBDConnector::is_connected() const {

    return is_connected_;

}



std::string OBDConnector::send_tcp_command(const std::string& cmd, int timeout_ms) {

    std::lock_guard<std::mutex> lock(socket_mutex_);



    if (tcp_socket_ == INVALID_SOCKET) {

        std::cerr << "Ошибка: сокет не инициализирован" << std::endl;

        return "";

    }



    std::string cmd_with_term = cmd + "\r";

    std::cout << "Отправка TCP команды: " << cmd << std::endl;



#ifdef _WIN32

    int sent = send(tcp_socket_, cmd_with_term.c_str(), static_cast<int>(cmd_with_term.length()), 0);

    if (sent == SOCKET_ERROR) {

        int error = WSAGetLastError();

        std::cerr << "Ошибка отправки TCP команды: " << error << std::endl;

        return "";

    }

#else

    int sent = write(tcp_socket_, cmd_with_term.c_str(), cmd_with_term.length());

    if (sent < 0) {

        std::cerr << "Ошибка отправки TCP команды: " << strerror(errno) << std::endl;

        return "";

    }

#endif



    char buffer[4096];

    auto start = std::chrono::steady_clock::now();

    std::string response;

    int bytes_received = 0;

    int no_data_count = 0;



    while (std::chrono::duration_cast<std::chrono::milliseconds>(

        std::chrono::steady_clock::now() - start).count() < timeout_ms) {



#ifdef _WIN32

        int bytes = recv(tcp_socket_, buffer, sizeof(buffer) - 1, 0);

        if (bytes == SOCKET_ERROR) {

            int err = WSAGetLastError();

            if (err == WSAETIMEDOUT) {

                no_data_count++;

                if (no_data_count > 10) { // ���� ������� ����� ��������� ������

                    std::this_thread::sleep_for(std::chrono::milliseconds(50));

                    continue;

                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                continue;

            }

            std::cerr << "Ошибка чтения TCP данных: " << err << std::endl;

            break;

        }

#else

        int bytes = read(tcp_socket_, buffer, sizeof(buffer) - 1);

        if (bytes < 0) {

            if (errno == EAGAIN || errno == EWOULDBLOCK) {

                no_data_count++;

                if (no_data_count > 10) {

                    std::this_thread::sleep_for(std::chrono::milliseconds(50));

                    continue;

                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                continue;

            }

            std::cerr << "Ошибка чтения TCP данных: " << strerror(errno) << std::endl;

            break;

        }

#endif

        if (bytes > 0) {

            bytes_received += bytes;

            buffer[bytes] = '\0';

            response += buffer;

            no_data_count = 0; // ���������� ������� ��� ��������� ������



            if (response.find('>') != std::string::npos) {

                break;

            }

        }

        else {

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        }

    }



    size_t pos = response.find('>');

    if (pos != std::string::npos) {

        response = response.substr(0, pos);

    }



    // ������� �� ������ ��������

    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());

    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());



    if (!response.empty()) {

        std::cout << "Ответ на '" << cmd << "': " << response << std::endl;

    }

    else {

        std::cout << "Пустой ответ на команду '" << cmd << "'" << std::endl;

    }



    return response;

}



bool OBDConnector::send_tcp_raw(const std::string& data) {

    std::lock_guard<std::mutex> lock(socket_mutex_);



    if (tcp_socket_ == INVALID_SOCKET) {

        return false;

    }



#ifdef _WIN32

    int sent = send(tcp_socket_, data.c_str(), static_cast<int>(data.length()), 0);

    return sent != SOCKET_ERROR;

#else

    int sent = write(tcp_socket_, data.c_str(), data.length());

    return sent >= 0;

#endif

}



std::string OBDConnector::send_command(const std::string& command, int timeout_ms) {

    if (!is_connected_) return "";



    if (is_tcp_mode_) {

        return send_tcp_command(command, timeout_ms);

    }



    serial_port_->flush();

    serial_port_->write(command + "\r");



    std::string response;

    char buffer[256];

    auto start = std::chrono::steady_clock::now();



    while (std::chrono::duration_cast<std::chrono::milliseconds>(

        std::chrono::steady_clock::now() - start).count() < timeout_ms) {



        int bytes = serial_port_->read(buffer, sizeof(buffer) - 1);

        if (bytes > 0) {

            buffer[bytes] = '\0';

            response += buffer;



            if (response.find('>') != std::string::npos) {

                break;

            }

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    }



    size_t pos = response.find('>');

    if (pos != std::string::npos) {

        response = response.substr(0, pos);

    }



    response.erase(std::remove(response.begin(), response.end(), '\r'), response.end());

    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());

    response.erase(std::remove(response.begin(), response.end(), ' '), response.end());



    return response;

}



std::string OBDConnector::send_at_command(const std::string& command) {

    return send_command("AT" + command);

}



std::string OBDConnector::send_obd_command(const std::string& pid) {

    return send_command(pid);

}



bool OBDConnector::initialize_adapter() {

    if (!is_connected_) return false;



    std::cout << "Инициализация ELM327 адаптера..." << std::endl;



    if (!is_tcp_mode_) {
        // Шаг 1: посылаем несколько \r чтобы прервать режим ATMA (мониторинг CAN).
        // В режиме мониторинга ELM327 игнорирует команды, но любой символ его останавливает.
        for (int i = 0; i < 5; i++) {
            serial_port_->write("\r");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        // Шаг 2: дрейн фиксированное время — выбрасываем всё (CAN-буфер + ответ на \r).
        // Фиксированный интервал: нельзя ждать тишины на активной CAN-шине.
        serial_port_->flush();
        char drain_buf[256];
        auto drain_start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - drain_start).count() < 500) {
            serial_port_->read(drain_buf, sizeof(drain_buf));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        serial_port_->flush();
    }

    // Диагностика: читаем сырые байты до ATZ (что сейчас в буфере)
    if (!is_tcp_mode_) {
        char raw_buf[128];
        int raw_n = serial_port_->read(raw_buf, sizeof(raw_buf) - 1);
        if (raw_n > 0) {
            std::cout << "RAW pre-ATZ (" << raw_n << " bytes): ";
            for (int i = 0; i < raw_n; i++) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << (static_cast<unsigned int>(raw_buf[i]) & 0xFF) << " ";
            }
            std::cout << std::dec << std::endl;
        } else {
            std::cout << "RAW pre-ATZ: (empty)" << std::endl;
        }
    }

    // ATZ: сброс адаптера. Ответ содержит "ELM327 v..." — используем его как основную проверку.
    // Пример ответа: "\r\nELM327 v2.1\r\n>"
    std::string atz_response = send_command("ATZ", 3000);

    // Диагностика: показываем сырые байты ATZ-ответа до strip
    std::cout << "RAW ATZ response (" << atz_response.size() << " chars after strip): [";
    for (unsigned char c : atz_response) {
        if (c >= 0x20 && c < 0x7F) std::cout << c;
        else std::cout << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)c << std::dec;
    }
    std::cout << "]" << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    if (!is_tcp_mode_) {
        serial_port_->flush();
    }

    // Проверяем ответ ATZ: он уже содержит версию адаптера
    auto contains = [](const std::string& s, const char* sub) {
        return s.find(sub) != std::string::npos;
    };
    bool valid = contains(atz_response, "ELM") || contains(atz_response, "elm") ||
                 contains(atz_response, "STN") || contains(atz_response, "OBD");

    if (valid) {
        std::cout << "ELM327 обнаружен (ATZ): " << atz_response.substr(0, 50) << std::endl;
    } else {
        // Резервный вариант: конфигурируем и запрашиваем ATI
        send_command("ATE0", 500);
        if (!is_tcp_mode_) serial_port_->flush();

        std::string ati_response = send_command("ATI", 2000);
        std::cout << "ELM327 версия (ATI): " << ati_response.substr(0, 50) << std::endl;

        valid = contains(ati_response, "ELM") || contains(ati_response, "elm") ||
                contains(ati_response, "STN") || contains(ati_response, "OBD");
    }

    if (!valid) {
        std::cout << "Ответ не похож на ELM327 (ATZ): " << atz_response.substr(0, 32) << std::endl;
        return false;
    }

    // Конфигурируем адаптер после успешной проверки
    send_command("ATE0", 500); // Эхо выкл
    send_command("ATH0", 500); // Заголовки CAN выкл — parse_response ожидает формат без них
    send_command("ATL0", 500); // Перевод строки выкл — чище парсить
    send_command("ATS0", 500); // Пробелы в ответе выкл — убираем двойное удаление
    send_command("ATSP0", 500); // Автовыбор протокола
    return true;
}



bool OBDConnector::set_protocol(int protocol_id) {

    std::string cmd = "SP " + std::to_string(protocol_id);

    std::string response = send_at_command(cmd);

    return response.find("OK") != std::string::npos;

}



// ==================== OBD-II ������ ====================



double OBDConnector::read_parameter(const std::string& pid) {

    if (!is_connected_) return 0.0;



    std::string response = send_obd_command(pid);

    return parse_response(response, pid);

}



std::map<std::string, double> OBDConnector::read_multiple_parameters(const std::vector<std::string>& pids) {

    std::map<std::string, double> results;



    for (const auto& pid : pids) {

        results[pid] = read_parameter(pid);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    }

    return results;

}



std::vector<DTCCode> OBDConnector::read_dtc_codes() {

    std::vector<DTCCode> dtc_codes;

    if (!is_connected_) return dtc_codes;



    std::string response = send_obd_command("03");



    if (!response.empty() && response != "NODATA") {

        return parse_dtc_response(response);

    }



    return dtc_codes;

}



bool OBDConnector::clear_dtc_codes() {

    if (!is_connected_) return false;



    std::string response = send_obd_command("04");

    return response.find("44") != std::string::npos;

}



std::string OBDConnector::read_vin() {

    if (!is_connected_) return "";

    std::string response = send_obd_command("0902");



    if (response.size() > 20) {

        std::string vin;

        for (size_t i = 4; i < response.size(); i += 2) {

            if (i + 1 < response.size()) {

                std::string hex_byte = response.substr(i, 2);

                if (hex_byte != "00") {

                    int ascii_val = hex_to_int(hex_byte);

                    if (ascii_val >= 32 && ascii_val <= 126) {

                        vin += static_cast<char>(ascii_val);

                    }

                }

            }

        }

        return vin;

    }

    return "";

}



// ==================== KWP2000 ������ ====================



bool OBDConnector::initialize_kwp2000() {

    if (!is_connected_) return false;



    std::cout << "Инициализация KWP2000 протокола..." << std::endl;



    // ������������� �������� KWP2000 (fast init, 10400 ���)

    if (!set_protocol(10)) { // ATSP A - KWP2000 fast init

        std::cout << "Не удалось установить протокол KWP2000" << std::endl;

        return false;

    }



    std::this_thread::sleep_for(std::chrono::milliseconds(500));



    // �������� ���������� ��������������� ������

    if (!kwp2000_start_session(0x81)) { // 0x81 - ����������� ��������������� ������

        std::cout << "Не удалось запустить диагностическую сессию KWP2000" << std::endl;

        return false;

    }



    std::cout << "KWP2000 инициализирован успешно" << std::endl;

    return true;

}



std::vector<uint8_t> OBDConnector::send_kwp2000_command(const std::vector<uint8_t>& command, int timeout_ms) {

    std::vector<uint8_t> result;



    if (!is_connected_) return result;



    // ������������ ������� � hex ������ ��� ELM327

    std::stringstream ss;

    for (size_t i = 0; i < command.size(); i++) {

        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')

            << static_cast<int>(command[i]);

    }



    std::string hex_command = ss.str();

    std::string response = send_command(hex_command, timeout_ms);



    if (!response.empty()) {

        result = hex_to_bytes(response);

    }



    return result;

}



bool OBDConnector::kwp2000_start_session(uint8_t session_type) {

    // ��������� ������� KWP2000: 0x10 (StartDiagnosticSession)

    std::vector<uint8_t> command;

    command.push_back(0x02); // ����� ������

    command.push_back(0x10); // ������

    command.push_back(session_type); // ��� ������



    auto response = send_kwp2000_command(command);



    // ��������� �����: ������ ���������� � 0x50 (������������� ����� �� 0x10)

    return response.size() >= 2 && response[1] == 0x50;

}



std::vector<DTCCode> OBDConnector::read_kwp2000_dtc() {

    std::vector<DTCCode> dtc_codes;



    if (!is_connected_) return dtc_codes;



    // ������� KWP2000: 0x13 (ReadDiagnosticTroubleCodes)

    std::vector<uint8_t> command;

    command.push_back(0x01); // ����� ������

    command.push_back(0x13); // ������



    auto response = send_kwp2000_command(command);



    if (!response.empty()) {

        dtc_codes = parse_kwp2000_dtc(response);

    }



    return dtc_codes;

}



bool OBDConnector::clear_kwp2000_dtc() {

    // ������� KWP2000: 0x14 (ClearDiagnosticInformation)

    std::vector<uint8_t> command;

    command.push_back(0x03); // ����� ������

    command.push_back(0x14); // ������

    command.push_back(0xFF); // ������ ������ (FF - ���)

    command.push_back(0xFF);



    auto response = send_kwp2000_command(command);



    return !response.empty() && response[1] == 0x54; // ������������� ����� �� 0x14

}



std::string OBDConnector::read_kwp2000_vin() {

    // ������� KWP2000: 0x22 (ReadDataByIdentifier) � ID = 0xF190 (VIN)

    std::vector<uint8_t> command;

    command.push_back(0x03); // ����� ������

    command.push_back(0x22); // ������

    command.push_back(0xF1); // ������� ���� ID

    command.push_back(0x90); // ������� ���� ID



    auto response = send_kwp2000_command(command);



    if (response.size() > 4) {

        // �����: [�����, 0x62 (������������� �����), ������...]

        std::string vin;

        for (size_t i = 3; i < response.size() - 1; i++) { // -1 ���������� checksum

            if (response[i] >= 0x20 && response[i] <= 0x7E) {

                vin += static_cast<char>(response[i]);

            }

        }

        return vin;

    }



    return "";

}



std::map<uint16_t, float> OBDConnector::read_kwp2000_parameters(const std::vector<uint16_t>& param_ids) {

    std::map<uint16_t, float> results;



    for (uint16_t id : param_ids) {

        // �������: 0x21 (ReadDataByLocalIdentifier) ��� 0x22 (ReadDataByIdentifier)

        std::vector<uint8_t> command;

        command.push_back(0x03); // ����� ������

        command.push_back(0x21); // ���������� ReadDataByLocalIdentifier

        command.push_back(static_cast<uint8_t>(id >> 8)); // ������� ����

        command.push_back(static_cast<uint8_t>(id & 0xFF)); // ������� ����



        auto response = send_kwp2000_command(command);



        if (response.size() > 4 && response[1] == 0x61) { // 0x61 - ������������� ����� �� 0x21

            // ������ ������ � ����������� �� ID ���������

            auto it = kwp2000_parameters_.find(id);

            if (it != kwp2000_parameters_.end()) {

                const auto& param = it->second;



                // ����������� ����� ������ � ��������

                float value = 0.0f;

                if (response.size() >= 5) {

                    // ������ �������� �������������� (������� �� ���������)

                    value = static_cast<float>(response[3]) * 256.0f + response[4];



                    // ��������� ������� ���� ����

                    if (!param.formula.empty()) {

                        // ����� ������ ���� ���������� �������� �������

                        // ��� �������� ���������� �������� ������������

                        value = value * param.max_value / 65535.0f;

                    }

                }

                results[id] = value;

            }

        }



        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    }



    return results;

}



bool OBDConnector::kwp2000_security_access(uint16_t key) {

    // ��� 1: ������ seed

    std::vector<uint8_t> seed_cmd;

    seed_cmd.push_back(0x02); // ����� ������

    seed_cmd.push_back(0x27); // ������ SecurityAccess

    seed_cmd.push_back(0x01); // ������ seed



    auto seed_response = send_kwp2000_command(seed_cmd);



    if (seed_response.size() < 4 || seed_response[1] != 0x67) {

        return false;

    }



    // ��� 2: �������� key (seed + ��������)

    std::vector<uint8_t> key_cmd;

    key_cmd.push_back(0x04); // ����� ������

    key_cmd.push_back(0x27); // ������

    key_cmd.push_back(0x02); // �������� key

    key_cmd.push_back(static_cast<uint8_t>(key >> 8)); // ������� ���� key

    key_cmd.push_back(static_cast<uint8_t>(key & 0xFF)); // ������� ���� key



    auto key_response = send_kwp2000_command(key_cmd);



    return key_response.size() >= 2 && key_response[1] == 0x67;

}



// ==================== ������� � ��������������� ������ ====================



double OBDConnector::parse_response(const std::string& response, const std::string& pid) {

    if (response.empty() || response.find("NODATA") != std::string::npos) {

        return 0.0;

    }



    std::string data = response;

    if (data.size() > 4) {

        data = data.substr(4); // ������� ���������

    }



    if (data.size() < 2) return 0.0;



    if (pid == "010C") { // RPM

        if (data.size() >= 4) {

            int a = hex_to_int(data.substr(0, 2));

            int b = hex_to_int(data.substr(2, 2));

            return (256.0 * a + b) / 4.0;

        }

    }

    else if (pid == "010D") { // Speed

        return hex_to_int(data.substr(0, 2));

    }

    else if (pid == "0105") { // Coolant temp

        return hex_to_int(data.substr(0, 2)) - 40;

    }

    else if (pid == "0111") { // Throttle position

        return hex_to_int(data.substr(0, 2)) * 100.0 / 255.0;

    }

    else if (pid == "0110") { // MAF air flow rate (g/s)

        if (data.size() >= 4) {

            int a = hex_to_int(data.substr(0, 2));

            int b = hex_to_int(data.substr(2, 2));

            return (256.0 * a + b) / 100.0;

        }

    }

    else if (pid == "010F") { // Intake air temperature

        return hex_to_int(data.substr(0, 2)) - 40;

    }

    else if (pid == "010A") { // Fuel pressure gauge (kPa)

        return hex_to_int(data.substr(0, 2)) * 3.0;

    }



    return hex_to_int(data.substr(0, 2));

}



std::vector<DTCCode> OBDConnector::parse_dtc_response(const std::string& response) {

    std::vector<DTCCode> dtc_codes;



    std::string data = response;

    if (data.size() > 4) {

        data = data.substr(4);

    }



    for (size_t i = 0; i + 3 < data.size(); i += 4) {

        std::string dtc_hex = data.substr(i, 4);



        if (dtc_hex == "0000") continue;



        DTCCode dtc;

        // Первый hex-символ кодирует и категорию, и вторую цифру кода.
        // '0'→P0, '1'→P1, '2'→P2, '3'→P3,
        // '4'→C0, '5'→C1, '6'→C2, '7'→C3,
        // '8'→B0, '9'→B1, 'A'→B2, 'B'→B3,
        // 'C'→U0, 'D'→U1, 'E'→U2, 'F'→U3
        char first_char = static_cast<char>(std::toupper(static_cast<unsigned char>(dtc_hex[0])));
        std::string prefix;
        std::string category_desc;
        switch (first_char) {
        case '0': prefix = "P0"; category_desc = "Powertrain"; break;
        case '1': prefix = "P1"; category_desc = "Powertrain"; break;
        case '2': prefix = "P2"; category_desc = "Powertrain"; break;
        case '3': prefix = "P3"; category_desc = "Powertrain"; break;
        case '4': prefix = "C0"; category_desc = "Chassis";    break;
        case '5': prefix = "C1"; category_desc = "Chassis";    break;
        case '6': prefix = "C2"; category_desc = "Chassis";    break;
        case '7': prefix = "C3"; category_desc = "Chassis";    break;
        case '8': prefix = "B0"; category_desc = "Body";       break;
        case '9': prefix = "B1"; category_desc = "Body";       break;
        case 'A': prefix = "B2"; category_desc = "Body";       break;
        case 'B': prefix = "B3"; category_desc = "Body";       break;
        case 'C': prefix = "U0"; category_desc = "Network";    break;
        case 'D': prefix = "U1"; category_desc = "Network";    break;
        case 'E': prefix = "U2"; category_desc = "Network";    break;
        case 'F': prefix = "U3"; category_desc = "Network";    break;
        default:  prefix = "?";  category_desc = "Unknown";    break;
        }

        // Формат кода: "P0143" = prefix("P0") + оставшиеся 3 символа hex("143")
        dtc.code = prefix + dtc_hex.substr(1);
        dtc.description = category_desc;



        dtc.status = "Active";

        dtc.timestamp = "N/A";

        dtc_codes.push_back(dtc);

    }



    return dtc_codes;

}



std::vector<DTCCode> OBDConnector::parse_kwp2000_dtc(const std::vector<uint8_t>& data) {

    std::vector<DTCCode> dtc_codes;



    if (data.size() < 3) return dtc_codes;



    // ���������� ��������� (�����, ������)

    size_t offset = 2;



    while (offset + 2 < data.size() - 1) { // -1 ��� checksum

        uint16_t dtc_raw = (data[offset] << 8) | data[offset + 1];

        offset += 2;



        if (dtc_raw == 0) continue;



        DTCCode dtc;



        // ����������� ��� ������ � ����� OBD-II

        uint8_t first_byte = dtc_raw >> 8;

        uint8_t second_byte = dtc_raw & 0xFF;



        char prefix;

        if ((first_byte & 0xC0) == 0x40) prefix = 'P';

        else if ((first_byte & 0xC0) == 0x80) prefix = 'C';

        else if ((first_byte & 0xC0) == 0xC0) prefix = 'B';

        else prefix = 'U';



        std::stringstream ss;

        ss << prefix << std::hex << std::uppercase

            << ((first_byte & 0x3F) << 8) << second_byte;



        dtc.code = ss.str();

        dtc.status = "Active";

        dtc.timestamp = "N/A";



        // ����� �������� �������� �� ���� ������

        dtc.description = "KWP2000 DTC: " + dtc.code;



        dtc_codes.push_back(dtc);

    }



    return dtc_codes;

}



KWP2000Packet OBDConnector::parse_kwp2000_response(const std::vector<uint8_t>& response) {

    KWP2000Packet packet;



    if (response.size() < 5) return packet;



    packet.header = response[0];

    packet.target_addr = response[1];

    packet.source_addr = response[2];

    packet.service_id = response[3];



    if (response.size() > 5) {

        packet.data.assign(response.begin() + 4, response.end() - 1);

    }



    packet.checksum = response.back();



    return packet;

}



uint8_t OBDConnector::calculate_kwp2000_checksum(const std::vector<uint8_t>& data) {

    uint8_t checksum = 0;

    for (uint8_t byte : data) {

        checksum += byte;

    }

    return checksum;

}



std::vector<uint8_t> OBDConnector::hex_to_bytes(const std::string& hex) {

    std::vector<uint8_t> bytes;



    for (size_t i = 0; i < hex.length(); i += 2) {

        std::string byteString = hex.substr(i, 2);

        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));

        bytes.push_back(byte);

    }



    return bytes;

}



std::string OBDConnector::bytes_to_hex(const std::vector<uint8_t>& bytes) {

    std::stringstream ss;

    for (uint8_t byte : bytes) {

        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')

            << static_cast<int>(byte);

    }

    return ss.str();

}



unsigned int OBDConnector::hex_to_int(const std::string& hex) {

    unsigned int value;

    std::stringstream ss;

    ss << std::hex << hex;

    ss >> value;

    return value;

}



std::string OBDConnector::int_to_hex(unsigned int value) {

    std::stringstream ss;

    ss << std::hex << std::uppercase << value;

    return ss.str();

}



void OBDConnector::initialize_parameters() {

    // ������� ��������� ��� OBD2

    parameters_.clear();

    parameters_["0100"] = OBDParameter("0100", "Supported PIDs 01-20", "", 0, 0, "");

    parameters_["0105"] = OBDParameter("0105", "Engine Coolant Temp", "�C", -40, 215, "A-40");

    parameters_["010C"] = OBDParameter("010C", "Engine RPM", "RPM", 0, 16383.75, "(256*A+B)/4");

    parameters_["010D"] = OBDParameter("010D", "Vehicle Speed", "km/h", 0, 255, "A");

    parameters_["0110"] = OBDParameter("0110", "MAF Flow Rate", "g/s", 0, 655.35, "(256*A+B)/100");

    parameters_["0111"] = OBDParameter("0111", "Throttle Position", "%", 0, 100, "100*A/255");

}



void OBDConnector::initialize_kwp2000_parameters() {

    // ������������� KWP2000 ����������

    kwp2000_parameters_.clear();



    KWP2000Parameter param;



    param.id = 0x0101;

    param.name = "Engine Speed";

    param.unit = "RPM";

    param.min_value = 0;

    param.max_value = 8000;

    param.formula = "raw*0.125";

    kwp2000_parameters_[0x0101] = param;



    param.id = 0x0102;

    param.name = "Vehicle Speed";

    param.unit = "km/h";

    param.min_value = 0;

    param.max_value = 255;

    param.formula = "raw";

    kwp2000_parameters_[0x0102] = param;



    param.id = 0x0103;

    param.name = "Coolant Temperature";

    param.unit = "�C";

    param.min_value = -40;

    param.max_value = 150;

    param.formula = "raw-40";

    kwp2000_parameters_[0x0103] = param;



    param.id = 0x0104;

    param.name = "Intake Air Temperature";

    param.unit = "�C";

    param.min_value = -40;

    param.max_value = 150;

    param.formula = "raw-40";

    kwp2000_parameters_[0x0104] = param;



    param.id = 0x0105;

    param.name = "Throttle Position";

    param.unit = "%";

    param.min_value = 0;

    param.max_value = 100;

    param.formula = "raw*0.392";

    kwp2000_parameters_[0x0105] = param;



    param.id = 0x0106;

    param.name = "Fuel Pressure";

    param.unit = "kPa";

    param.min_value = 0;

    param.max_value = 765;

    param.formula = "raw*3";

    kwp2000_parameters_[0x0106] = param;



    param.id = 0x0107;

    param.name = "Boost Pressure";

    param.unit = "kPa";

    param.min_value = 0;

    param.max_value = 400;

    param.formula = "raw";

    kwp2000_parameters_[0x0107] = param;



    param.id = 0x0108;

    param.name = "Ignition Timing";

    param.unit = "�BTDC";

    param.min_value = -64;

    param.max_value = 63.5f;

    param.formula = "raw*0.5-64";

    kwp2000_parameters_[0x0108] = param;

}



bool OBDConnector::send_keep_alive() {

    if (!is_connected_) return false;



    if (is_tcp_mode_) {

        std::string response = send_tcp_command("ATI", 500);

        return !response.empty();

    }

    else {

        std::string response = send_at_command("I");

        return !response.empty();

    }

}



bool OBDConnector::test_connection() {

    if (!is_connected_) return false;



    std::string response = send_at_command("I");

    return !response.empty();

}



bool OBDConnector::test_can_bus() {

    if (!is_connected_) return false;



    std::string response = send_command("ATCS", 1000);

    return !response.empty();

}



bool OBDConnector::tester_present() {

    if (!is_connected_) return false;



    std::string response = send_at_command("TP");

    return response.find("OK") != std::string::npos;

}



void OBDConnector::start_realtime_monitoring(const std::vector<std::string>& pids) {

    (void)pids; // �������� ��� ������� ����������

}



void OBDConnector::stop_realtime_monitoring() {

    // �������� ��� ������� ����������

}



std::map<std::string, double> OBDConnector::get_current_values() {

    return std::map<std::string, double>();

}



std::vector<std::string> OBDConnector::get_supported_pids() {

    std::vector<std::string> pids;

    if (!is_connected_) return pids;



    // ������ �������������� PID (0100)

    std::string response = send_obd_command("0100");



    if (!response.empty() && response.find("NODATA") == std::string::npos) {

        // ������� PID ������� ������ ��������������

        pids = { "0100", "0105", "010C", "010D", "0110", "0111" };

    }



    return pids;

}



// ==================== CAN ������ ====================



bool OBDConnector::initialize_can() {

    if (!is_connected_) return false;



    std::cout << "Инициализация CAN через ELM327..." << std::endl;



    // ������������� �������� CAN (ISO 15765-4, 11 bit ID, 500kbps)

    if (!set_protocol(6)) { // ATSP 6 - CAN 11/500

        std::cout << "Не удалось установить протокол CAN" << std::endl;

        return false;

    }



    // ����������� CAN

    send_at_command("CSM1"); // �������� CAN ����������

    send_at_command("CRA 7E8"); // ���������� ����� ��� ������ (������ 0x7E8)

    send_at_command("SH 7DF"); // ���������� ��������� (������ 0x7DF ��� ��������)



    std::this_thread::sleep_for(std::chrono::milliseconds(500));



    // ��������� CAN ����

    std::string response = send_obd_command("0100");

    if (!response.empty() && response != "NODATA") {

        std::cout << "CAN инициализирован успешно" << std::endl;

        return true;

    }



    return false;

}



bool OBDConnector::set_can_baud_rate(uint32_t baud_rate) {

    std::string cmd;

    switch (baud_rate) {

    case 50000: cmd = "ATCB 0"; break;

    case 125000: cmd = "ATCB 1"; break;

    case 250000: cmd = "ATCB 2"; break;

    case 500000: cmd = "ATCB 3"; break;

    case 1000000: cmd = "ATCB 4"; break;

    default: return false;

    }



    std::string response = send_at_command(cmd);

    return response.find("OK") != std::string::npos;

}



bool OBDConnector::send_can_message(uint32_t id, const std::vector<uint8_t>& data, bool is_extended) {

    if (!is_connected_ || data.size() > 8) return false;



    // ATSH — устанавливает заголовок (ID) для ПЕРЕДАЧИ. Раньше здесь ошибочно
    // использовался ATCRA (это фильтр ПРИЁМА, а не адрес отправителя).

    std::stringstream hdr_ss;

    hdr_ss << "SH " << std::hex << std::uppercase
        << std::setw(is_extended ? 8 : 3) << std::setfill('0') << id;

    send_at_command(hdr_ss.str());



    // Полезная нагрузка отправляется как обычная hex-строка через уже рабочий
    // send_command() (как в send_kwp2000_command) — ELM327 сам делает ISO-TP
    // сегментацию и сразу возвращает ответ ЭБУ на эту же команду.
    // ATCT/CRR, использовавшиеся здесь раньше, не являются реальными AT-командами.

    std::stringstream data_ss;

    for (size_t i = 0; i < data.size(); i++) {

        data_ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')

            << static_cast<int>(data[i]);

    }



    last_can_response_ = send_command(data_ss.str());

    return !last_can_response_.empty()
        && last_can_response_.find("NO DATA") == std::string::npos
        && last_can_response_.find("ERROR") == std::string::npos;

}



std::vector<CANMessage> OBDConnector::read_can_messages(int timeout_ms) {

    (void)timeout_ms;

    std::vector<CANMessage> messages;



    // Разбирает последний ответ, полученный send_can_message(). Формат "ID+данные"
    // корректен только при включённых заголовках (ATH1) — так их включает
    // discover_can_nodes() на время сканирования. Настоящий потоковый мониторинг
    // шины (ATMA) этой функцией не читается — см. TODO в monitor_can_bus().

    if (!last_can_response_.empty()) {

        messages = parse_can_messages(last_can_response_);

    }



    return messages;

}



bool OBDConnector::set_can_filter(uint32_t id, uint32_t mask) {

    std::stringstream ss;

    ss << "ATCF " << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << id << ","

        << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << mask;



    std::string response = send_at_command(ss.str());

    return response.find("OK") != std::string::npos;

}



bool OBDConnector::start_can_monitoring(std::function<void(const CANMessage&)> callback) {

    if (monitoring_active_) return false;



    can_callback_ = callback;

    monitoring_active_ = true;



    monitor_thread_ = std::thread([this]() {

        while (monitoring_active_) {

            auto messages = read_can_messages(100);

            for (const auto& msg : messages) {

                if (can_callback_) {

                    can_callback_(msg);

                }

            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        }

        });



    return true;

}



void OBDConnector::stop_can_monitoring() {

    monitoring_active_ = false;

    if (monitor_thread_.joinable()) {

        monitor_thread_.join();

    }

}



std::vector<CANMessage> OBDConnector::parse_can_messages(const std::string& response) {

    std::vector<CANMessage> messages;



    std::istringstream iss(response);

    std::string line;



    while (std::getline(iss, line, '\r')) {

        if (line.empty() || line == ">") continue;



        // ������� �������

        line.erase(std::remove(line.begin(), line.end(), ' '), line.end());



        if (line.length() >= 6) { // ����������� �����: ID + ������

            CANMessage msg;

            msg.timestamp = std::chrono::system_clock::now();



            // ������ ID (������ 3 ��� 8 ��������)

            size_t id_len = 3;

            if (line.length() > 8 && (line[0] != '0' || line[1] != '0' || line[2] != '0')) {

                id_len = 8;

            }

            msg.is_extended = (id_len == 8);



            std::string id_str = line.substr(0, id_len);

            msg.id = std::stoul(id_str, nullptr, 16);



            // ������ ������

            std::string data_str = line.substr(id_len);

            for (size_t i = 0; i + 1 < data_str.length(); i += 2) {

                std::string byte_str = data_str.substr(i, 2);

                msg.data.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));

            }



            messages.push_back(msg);

        }

    }



    return messages;

}



// ==================== UDS ������ ====================



bool OBDConnector::initialize_uds() {

    std::cout << "Инициализация UDS..." << std::endl;



    // UDS ������ �������� ������ CAN

    if (!initialize_can()) {

        return false;

    }



    // �������� ���������� ��������������� ������

    if (!uds_diagnostic_session_control(0x01)) { // 0x01 = default session

        std::cout << "Не удалось установить диагностическую сессию UDS" << std::endl;

        return false;

    }



    // ��������� �����

    if (!tester_present()) {

        std::cout << "ECU не отвечает на tester present" << std::endl;

        return false;

    }



    std::cout << "UDS инициализирован успешно" << std::endl;

    return true;

}



UDSPacket OBDConnector::send_uds_request(uint8_t service_id, const std::vector<uint8_t>& data) {

    UDSPacket result;

    // 0x7F = негативный ответ ("нет ответа" тоже считаем негативным, а не тихо
    // подменяем эхом запрошенного service_id — раньше отсутствие ответа выглядело
    // как успех для has_negative_response()).

    result.service_id = 0x7F;



    std::vector<uint8_t> request;

    request.push_back(service_id);

    request.insert(request.end(), data.begin(), data.end());



    // Физическая адресация текущей UDS-сессии (по умолчанию функциональный
    // запрос 0x7DF/0x7E8, см. set_uds_target()).

    if (!send_can_message(uds_tx_id_, request)) {

        return result;

    }



    // ELM327 в режиме ATH0 (заголовки выключены, initialize_adapter()) возвращает
    // голый hex-ответ ЭБУ на ту же команду — как и в send_kwp2000_command,
    // отдельного опроса не требуется.

    auto reply = hex_to_bytes(last_can_response_);

    if (!reply.empty()) {

        result.service_id = reply[0];

        if (reply.size() > 1) {

            result.data.assign(reply.begin() + 1, reply.end());

        }

    }



    return result;

}



void OBDConnector::set_uds_target(uint32_t tx_id, uint32_t rx_id) {

    uds_tx_id_ = tx_id;

    uds_rx_id_ = rx_id;



    std::stringstream ss;

    ss << "CRA " << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << rx_id;

    send_at_command(ss.str());

}



void OBDConnector::set_security_key_provider(std::shared_ptr<ISecurityKeyProvider> provider) {

    security_provider_ = provider ? provider : std::make_shared<NoKeyProvider>();

}



std::vector<uint8_t> OBDConnector::uds_read_data_by_identifier(uint16_t did) {

    std::vector<uint8_t> data;



    // ������ 0x22 - ReadDataByIdentifier

    std::vector<uint8_t> request;

    request.push_back(static_cast<uint8_t>(did >> 8));   // High byte

    request.push_back(static_cast<uint8_t>(did & 0xFF)); // Low byte



    auto response = send_uds_request(0x22, request);



    if (!response.has_negative_response() && response.data.size() >= 2) {

        // �������� �����: ������ 2 ����� - DID, ��������� - ������

        if (response.data.size() > 2) {

            data.assign(response.data.begin() + 2, response.data.end());

        }

    }

    else {

        uint8_t nrc = response.get_negative_response_code();

        std::cout << "UDS отрицательный ответ: 0x" << std::hex << (int)nrc << std::dec << std::endl;

    }



    return data;

}



bool OBDConnector::uds_write_data_by_identifier(uint16_t did, const std::vector<uint8_t>& data) {

    // ������ 0x2E - WriteDataByIdentifier

    std::vector<uint8_t> request;

    request.push_back(static_cast<uint8_t>(did >> 8));

    request.push_back(static_cast<uint8_t>(did & 0xFF));

    request.insert(request.end(), data.begin(), data.end());



    auto response = send_uds_request(0x2E, request);



    return !response.has_negative_response();

}



std::vector<DTCCode> OBDConnector::uds_read_dtc(uint8_t dtc_type) {

    std::vector<DTCCode> dtc_codes;



    // ������ 0x19 - ReadDTCInformation

    // ���������� 0x02 - Read DTC by status mask

    std::vector<uint8_t> request;

    request.push_back(0x02); // reportDTCByStatusMask

    request.push_back(dtc_type); // DTC status mask



    auto response = send_uds_request(0x19, request);



    if (!response.has_negative_response() && response.data.size() >= 2) {

        dtc_codes = parse_uds_dtc(response.data);

    }



    return dtc_codes;

}



bool OBDConnector::uds_clear_dtc(uint16_t group) {

    // ������ 0x14 - ClearDiagnosticInformation

    std::vector<uint8_t> request;

    request.push_back(static_cast<uint8_t>(group >> 8));

    request.push_back(static_cast<uint8_t>(group & 0xFF));



    auto response = send_uds_request(0x14, request);



    return !response.has_negative_response();

}



bool OBDConnector::uds_diagnostic_session_control(uint8_t session_type) {

    // ������ 0x10 - DiagnosticSessionControl

    std::vector<uint8_t> request;

    request.push_back(session_type);



    auto response = send_uds_request(0x10, request);



    if (!response.has_negative_response() && response.data.size() >= 2) {

        // �������� ����� �������� sessionType � timing parameters

        return true;

    }



    return false;

}



uint8_t OBDConnector::uds_ecu_reset(uint8_t reset_type) {

    // ������ 0x11 - ECUReset

    std::vector<uint8_t> request;

    request.push_back(reset_type);



    auto response = send_uds_request(0x11, request);



    if (!response.has_negative_response() && !response.data.empty()) {

        // ����� �������� resetType � ����������� powerDownTime

        return response.data[0];

    }



    return 0;

}



bool OBDConnector::uds_security_access(uint8_t access_type, const std::vector<uint8_t>& key) {

    // ������ 0x27 - SecurityAccess

    std::vector<uint8_t> request;

    request.push_back(access_type);

    request.insert(request.end(), key.begin(), key.end());



    auto response = send_uds_request(0x27, request);



    if (!response.has_negative_response()) {

        if (access_type == 0x01 || access_type == 0x03 || access_type == 0x05) {

            // ������ seed'�

            if (response.data.size() >= 1) {

                std::cout << "Получен seed: ";

                for (uint8_t byte : response.data) {

                    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte;

                }

                std::cout << std::dec << std::endl;

            }

        }

        return true;

    }



    return false;

}



bool OBDConnector::uds_unlock_security(uint8_t seed_level) {

    // Шаг 1: запросить seed (нечётный access_type).

    auto seed_response = send_uds_request(0x27, { seed_level });

    if (seed_response.has_negative_response()) {

        return false;

    }



    // Позитивный ответ 0x67: [эхо access_type][seed...] — сам seed начинается
    // со второго байта (раньше здесь ошибочно эхо-байт считался частью seed'а).
    std::vector<uint8_t> seed;
    if (seed_response.data.size() > 1) {
        seed.assign(seed_response.data.begin() + 1, seed_response.data.end());
    }

    // ЭБУ в незаблокированном состоянии иногда отвечает пустым/нулевым seed'ом —
    // это уже разблокировано, ключ не нужен.
    if (seed.empty() || std::all_of(seed.begin(), seed.end(),
            [](uint8_t b) { return b == 0; })) {

        return true;

    }



    // Шаг 2: посчитать key через подключаемый провайдер (бросает исключение,
    // если провайдер не настроен — см. NoKeyProvider).

    std::vector<uint8_t> key = security_provider_->compute_key(seed_level, seed);



    // Шаг 3: отправить key (следующий чётный access_type).

    auto key_response = send_uds_request(0x27, [&]() {

        std::vector<uint8_t> req = { static_cast<uint8_t>(seed_level + 1) };

        req.insert(req.end(), key.begin(), key.end());

        return req;

    }());



    return !key_response.has_negative_response();

}



std::vector<uint8_t> OBDConnector::uds_read_memory_by_address(uint32_t address, uint16_t size, uint8_t addr_len_fmt) {

    // 0x23 ReadMemoryByAddress: [addressAndLengthFormatIdentifier][memoryAddress][memorySize]
    // addr_len_fmt: старший полубайт — байты memorySize, младший — байты memoryAddress.

    uint8_t addr_bytes = addr_len_fmt & 0x0F;

    uint8_t size_bytes = (addr_len_fmt >> 4) & 0x0F;



    std::vector<uint8_t> request;

    request.push_back(addr_len_fmt);

    for (int i = static_cast<int>(addr_bytes) - 1; i >= 0; --i) {

        request.push_back(static_cast<uint8_t>((address >> (8 * i)) & 0xFF));

    }

    for (int i = static_cast<int>(size_bytes) - 1; i >= 0; --i) {

        request.push_back(static_cast<uint8_t>((size >> (8 * i)) & 0xFF));

    }



    auto response = send_uds_request(0x23, request);

    if (response.has_negative_response()) {

        return {};

    }

    return response.data;

}



bool OBDConnector::uds_request_download(uint32_t address, uint32_t size, uint16_t& out_block_size, uint8_t addr_len_fmt) {

    // 0x34 RequestDownload: [dataFormatIdentifier][addressAndLengthFormatIdentifier][memoryAddress][memorySize]
    // Позитивный ответ 0x74: [lengthFormatIdentifier][maxNumberOfBlockLength]

    uint8_t addr_bytes = addr_len_fmt & 0x0F;

    uint8_t size_bytes = (addr_len_fmt >> 4) & 0x0F;



    std::vector<uint8_t> request;

    request.push_back(0x00); // dataFormatIdentifier: без сжатия/шифрования

    request.push_back(addr_len_fmt);

    for (int i = static_cast<int>(addr_bytes) - 1; i >= 0; --i) {

        request.push_back(static_cast<uint8_t>((address >> (8 * i)) & 0xFF));

    }

    for (int i = static_cast<int>(size_bytes) - 1; i >= 0; --i) {

        request.push_back(static_cast<uint8_t>((size >> (8 * i)) & 0xFF));

    }



    auto response = send_uds_request(0x34, request);

    if (response.has_negative_response() || response.data.empty()) {

        out_block_size = 0;

        return false;

    }



    uint8_t len_fmt = response.data[0];

    uint8_t max_len_bytes = (len_fmt >> 4) & 0x0F;

    uint32_t max_block = 0;

    for (size_t i = 0; i < max_len_bytes && (1 + i) < response.data.size(); ++i) {

        max_block = (max_block << 8) | response.data[1 + i];

    }



    // -2: в каждом блоке TransferData первые 2 байта — service id (0x36) и
    // blockSequenceCounter, полезной нагрузки в блоке меньше на них.

    out_block_size = (max_block > 2) ? static_cast<uint16_t>(max_block - 2) : 0;

    return out_block_size > 0;

}



bool OBDConnector::uds_transfer_data(uint8_t block_seq, const std::vector<uint8_t>& chunk) {

    // 0x36 TransferData: [blockSequenceCounter][data...]

    std::vector<uint8_t> request;

    request.push_back(block_seq);

    request.insert(request.end(), chunk.begin(), chunk.end());



    auto response = send_uds_request(0x36, request);

    return !response.has_negative_response();

}



bool OBDConnector::uds_request_transfer_exit() {

    // 0x37 RequestTransferExit — без параметров (без контрольной суммы transferRequestParameterRecord).

    auto response = send_uds_request(0x37, {});

    return !response.has_negative_response();

}



std::vector<uint8_t> OBDConnector::uds_routine_control(uint8_t routine_id, const std::vector<uint8_t>& params) {

    // ������ 0x31 - RoutineControl

    std::vector<uint8_t> request;

    request.push_back(0x01); // startRoutine

    request.push_back(static_cast<uint8_t>(routine_id >> 8));

    request.push_back(static_cast<uint8_t>(routine_id & 0xFF));

    request.insert(request.end(), params.begin(), params.end());



    auto response = send_uds_request(0x31, request);



    if (!response.has_negative_response()) {

        return response.data;

    }



    return {};

}



std::vector<DTCCode> OBDConnector::parse_uds_dtc(const std::vector<uint8_t>& data) {

    std::vector<DTCCode> dtc_codes;



    // ������ ������ UDS ��� DTC:

    // [DTCStatusAvailabilityMask][DTCCount][DTC1_H][DTC1_L][Status1]...



    size_t offset = 2; // ���������� DTCStatusAvailabilityMask � DTCCount



    while (offset + 3 <= data.size()) {

        DTCCode dtc;



        // ��������� ��� DTC (3 �����)

        uint32_t dtc_raw = (static_cast<uint32_t>(data[offset]) << 16) |

            (static_cast<uint32_t>(data[offset + 1]) << 8) |

            static_cast<uint32_t>(data[offset + 2]);

        offset += 3;



        if (dtc_raw == 0) continue;



        // ����������� � ������ OBD-II �����

        char prefix;

        uint8_t first_byte = (dtc_raw >> 16) & 0xFF;

        uint8_t second_byte = (dtc_raw >> 8) & 0xFF;

        uint8_t third_byte = dtc_raw & 0xFF;



        // ���������� ��� DTC �� ������� �����

        switch ((first_byte & 0xC0) >> 6) {

        case 0: prefix = 'P'; break;

        case 1: prefix = 'C'; break;

        case 2: prefix = 'B'; break;

        case 3: prefix = 'U'; break;

        default: prefix = '?';

        }



        char code_str[10];

        snprintf(code_str, sizeof(code_str), "%c%02X%02X%02X", prefix,

            first_byte & 0x3F, second_byte, third_byte);

        dtc.code = code_str;



        // ������ DTC (��������� ����)

        if (offset < data.size()) {

            uint8_t status = data[offset++];

            std::bitset<8> status_bits(status);



            if (status_bits[0]) dtc.status = "TestFailed";

            else if (status_bits[1]) dtc.status = "TestFailedThisOperationCycle";

            else if (status_bits[2]) dtc.status = "Pending";

            else if (status_bits[3]) dtc.status = "Confirmed";

            else if (status_bits[4]) dtc.status = "TestNotCompletedSinceLastClear";

            else dtc.status = "Historical";

        }



        dtc.description = "UDS DTC: " + dtc.code;



        dtc_codes.push_back(dtc);

    }



    return dtc_codes;

}



void OBDConnector::initialize_uds_parameters() {

    // ������������� UDS ���������� (Data Identifiers)

    uds_parameters_.clear();



    UDSParameter param;



    param.did = 0xF180;

    param.name = "VIN";

    param.unit = "";

    param.min_value = 0;

    param.max_value = 0;

    param.formula = "";

    param.access_type = 1;

    uds_parameters_[0xF180] = param;



    param.did = 0xF190;

    param.name = "ECU Serial Number";

    param.unit = "";

    param.min_value = 0;

    param.max_value = 0;

    param.formula = "";

    param.access_type = 1;

    uds_parameters_[0xF190] = param;



    param.did = 0xF1A0;

    param.name = "Software Version";

    param.unit = "";

    param.min_value = 0;

    param.max_value = 0;

    param.formula = "";

    param.access_type = 1;

    uds_parameters_[0xF1A0] = param;



    param.did = 0xF1B0;

    param.name = "Hardware Version";

    param.unit = "";

    param.min_value = 0;

    param.max_value = 0;

    param.formula = "";

    param.access_type = 1;

    uds_parameters_[0xF1B0] = param;



    param.did = 0xF1C0;

    param.name = "Boot Software Version";

    param.unit = "";

    param.min_value = 0;

    param.max_value = 0;

    param.formula = "";

    param.access_type = 1;

    uds_parameters_[0xF1C0] = param;



    param.did = 0xF1D0;

    param.name = "Application Data Version";

    param.unit = "";

    param.min_value = 0;

    param.max_value = 0;

    param.formula = "";

    param.access_type = 1;

    uds_parameters_[0xF1D0] = param;



    param.did = 0xF1E0;

    param.name = "Manufacturer Code";

    param.unit = "";

    param.min_value = 0;

    param.max_value = 0;

    param.formula = "";

    param.access_type = 1;

    uds_parameters_[0xF1E0] = param;



    param.did = 0xF1F0;

    param.name = "Model Year";

    param.unit = "";

    param.min_value = 0;

    param.max_value = 0;

    param.formula = "";

    param.access_type = 1;

    uds_parameters_[0xF1F0] = param;



    param.did = 0xF200;

    param.name = "Engine RPM";

    param.unit = "rpm";

    param.min_value = 0;

    param.max_value = 8000;

    param.formula = "raw * 0.125";

    param.access_type = 1;

    uds_parameters_[0xF200] = param;



    param.did = 0xF201;

    param.name = "Vehicle Speed";

    param.unit = "km/h";

    param.min_value = 0;

    param.max_value = 250;

    param.formula = "raw";

    param.access_type = 1;

    uds_parameters_[0xF201] = param;



    param.did = 0xF202;

    param.name = "Coolant Temperature";

    param.unit = "�C";

    param.min_value = -40;

    param.max_value = 150;

    param.formula = "raw - 40";

    param.access_type = 1;

    uds_parameters_[0xF202] = param;



    param.did = 0xF203;

    param.name = "Fuel Level";

    param.unit = "%";

    param.min_value = 0;

    param.max_value = 100;

    param.formula = "raw * 0.392";

    param.access_type = 1;

    uds_parameters_[0xF203] = param;



    param.did = 0xF204;

    param.name = "Throttle Position";

    param.unit = "%";

    param.min_value = 0;

    param.max_value = 100;

    param.formula = "raw * 0.392";

    param.access_type = 1;

    uds_parameters_[0xF204] = param;



    param.did = 0xF205;

    param.name = "Intake Air Temperature";

    param.unit = "�C";

    param.min_value = -40;

    param.max_value = 150;

    param.formula = "raw - 40";

    param.access_type = 1;

    uds_parameters_[0xF205] = param;



    param.did = 0xF206;

    param.name = "Fuel Pressure";

    param.unit = "kPa";

    param.min_value = 0;

    param.max_value = 500;

    param.formula = "raw * 2";

    param.access_type = 1;

    uds_parameters_[0xF206] = param;



    param.did = 0xF207;

    param.name = "Boost Pressure";

    param.unit = "kPa";

    param.min_value = 0;

    param.max_value = 400;

    param.formula = "raw";

    param.access_type = 1;

    uds_parameters_[0xF207] = param;



    param.did = 0xF208;

    param.name = "Ignition Timing";

    param.unit = "�BTDC";

    param.min_value = -64;

    param.max_value = 64;

    param.formula = "raw * 0.5 - 64";

    param.access_type = 1;

    uds_parameters_[0xF208] = param;

}