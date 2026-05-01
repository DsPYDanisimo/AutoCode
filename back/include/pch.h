#ifndef PCH_H
#define PCH_H

// Глобальные определения для всего проекта
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#endif

// Стандартные библиотеки
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include <functional>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <optional>
#include <atomic>

#endif // PCH_H