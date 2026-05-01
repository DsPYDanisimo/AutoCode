#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <string>
#include <vector>

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    bool open(const std::string& port_name, unsigned int baud_rate = 115200);

    void close();

    bool is_open() const;

    int read(char* buffer, size_t size);

    int write(const char* data, size_t size);

    int write(const std::string& data);

    void flush();

    void set_timeout(int read_timeout_ms, int write_timeout_ms);

    static std::vector<std::string> list_ports();

private:
#ifdef _WIN32
    void* handle_;
#else
    int fd_;
#endif
    bool is_open_;
    std::string port_name_;
};

#endif 