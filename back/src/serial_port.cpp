#include "serial_port.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#endif

SerialPort::SerialPort() : is_open_(false) {
#ifdef _WIN32
    handle_ = INVALID_HANDLE_VALUE;
#else
    fd_ = -1;
#endif
}

SerialPort::~SerialPort() {
    close();
}

bool SerialPort::open(const std::string& port_name, unsigned int baud_rate) {
    close();
    port_name_ = port_name;

#ifdef _WIN32
    std::string full_port_name = "\\\\.\\" + port_name;
    handle_ = CreateFileA(
        full_port_name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Настройка параметров порта
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(handle_, &dcb)) {
        CloseHandle(handle_);
        return false;
    }

    dcb.BaudRate = baud_rate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle_, &dcb)) {
        CloseHandle(handle_);
        return false;
    }

    // Настройка таймаутов
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(handle_, &timeouts)) {
        CloseHandle(handle_);
        return false;
    }

    // Hardware reset: toggle DTR LOW then HIGH to force ELM327 chip reset
    // This breaks any stuck K-line/ATMA monitoring mode regardless of adapter state
    EscapeCommFunction(handle_, CLRDTR);
    Sleep(100);
    EscapeCommFunction(handle_, SETDTR);
    Sleep(500);
    // Discard boot message from adapter
    PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);

#else
    fd_ = ::open(port_name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (fd_ < 0) {
        return false;
    }

    termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd_, &tty) != 0) {
        ::close(fd_);
        return false;
    }

    // Настройка скорости
    speed_t speed;
    switch (baud_rate) {
    case 9600: speed = B9600; break;
    case 19200: speed = B19200; break;
    case 38400: speed = B38400; break;
    case 57600: speed = B57600; break;
    case 115200: speed = B115200; break;
    case 230400: speed = B230400; break;
    case 460800: speed = B460800; break;
    case 500000: speed = B500000; break;
    case 1000000: speed = B1000000; break;
    default: speed = B115200; break;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // Настройка параметров порта
    tty.c_cflag &= ~PARENB;    // No parity
    tty.c_cflag &= ~CSTOPB;    // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;        // 8 bits per byte
    tty.c_cflag &= ~CRTSCTS;   // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Enable reading

    tty.c_lflag &= ~ICANON;    // Non-canonical mode
    tty.c_lflag &= ~ECHO;      // Disable echo
    tty.c_lflag &= ~ECHOE;     // Disable erasure
    tty.c_lflag &= ~ECHONL;    // Disable new-line echo
    tty.c_lflag &= ~ISIG;      // Disable interpretation of INTR, QUIT and SUSP

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Disable software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_oflag &= ~OPOST; // Raw output
    tty.c_oflag &= ~ONLCR; // No conversion of newline

    // Настройка таймаутов
    tty.c_cc[VTIME] = 1; // 0.1 секунды между символами
    tty.c_cc[VMIN] = 0;  // Минимум 0 символов для чтения

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        ::close(fd_);
        return false;
    }

    tcflush(fd_, TCIOFLUSH);
#endif

    is_open_ = true;
    return true;
}

void SerialPort::close() {
    if (is_open_) {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        is_open_ = false;
    }
}

bool SerialPort::is_open() const {
    return is_open_;
}

int SerialPort::read(char* buffer, size_t size) {
    if (!is_open_) return -1;

#ifdef _WIN32
    DWORD bytes_read;
    if (!ReadFile(handle_, buffer, static_cast<DWORD>(size), &bytes_read, NULL)) {
        return -1;
    }
    return static_cast<int>(bytes_read);
#else
    return ::read(fd_, buffer, size);
#endif
}

int SerialPort::write(const char* data, size_t size) {
    if (!is_open_) return -1;

#ifdef _WIN32
    DWORD bytes_written;
    if (!WriteFile(handle_, data, static_cast<DWORD>(size), &bytes_written, NULL)) {
        return -1;
    }
    return static_cast<int>(bytes_written);
#else
    return ::write(fd_, data, size);
#endif
}

int SerialPort::write(const std::string& data) {
    return write(data.c_str(), data.length());
}

void SerialPort::flush() {
    if (!is_open_) return;

#ifdef _WIN32
    PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
    tcflush(fd_, TCIOFLUSH);
#endif
}

void SerialPort::set_timeout(int read_timeout_ms, int write_timeout_ms) {
    (void)read_timeout_ms;
    (void)write_timeout_ms;
    if (!is_open_) return;

#ifdef _WIN32
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = read_timeout_ms;
    timeouts.ReadTotalTimeoutConstant = read_timeout_ms;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = write_timeout_ms;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(handle_, &timeouts);
#else
    // Для Linux таймауты уже установлены через termios
#endif
}

std::vector<std::string> SerialPort::list_ports() {
    std::vector<std::string> ports;

#ifdef _WIN32
    char port_name[8];
    for (int i = 1; i <= 256; i++) {
        sprintf_s(port_name, "COM%d", i);
        HANDLE hPort = CreateFileA(port_name,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (hPort != INVALID_HANDLE_VALUE) {
            ports.push_back(port_name);
            CloseHandle(hPort);
        }
    }
#else
    // Linux: проверяем /dev/ttyUSB* и /dev/ttyACM*
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

    return ports;
}