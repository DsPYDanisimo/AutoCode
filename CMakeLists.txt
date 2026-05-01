cmake_minimum_required(VERSION 3.16)
project(ECU_Diagnostic_System VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Определение путей
set(BACKEND_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/back)

# Опции сборки
option(BUILD_TEST "Собрать тестовую программу" OFF)

# ЯВНО УКАЗЫВАЕМ ПУТЬ К SQLITE3
set(SQLITE3_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/include")
set(SQLITE3_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/lib/sqlite3.lib")

if(BUILD_TEST)
    # Сборка тестовой программы
    set(SOURCES
        ${BACKEND_SRC_DIR}/src/test_connection.cpp
        ${BACKEND_SRC_DIR}/src/obd_connector.cpp
        ${BACKEND_SRC_DIR}/src/serial_port.cpp
    )
    set(TARGET_NAME ecu_test)
    message(STATUS "⚙️ Сборка тестовой программы: ${TARGET_NAME}")
else()
    # Сборка основного сервера - УДАЛЯЕМ ecu_protocols.cpp
    set(SOURCES
        ${BACKEND_SRC_DIR}/src/action.cpp
        ${BACKEND_SRC_DIR}/src/obd_connector.cpp      # TCP методы уже здесь
        ${BACKEND_SRC_DIR}/src/serial_port.cpp
        ${BACKEND_SRC_DIR}/src/database.cpp
        ${BACKEND_SRC_DIR}/src/ecu_protocols.cpp    # ПОЛНОСТЬЮ УДАЛЕНО - не используется
    )
    set(TARGET_NAME ecu_backend)
    message(STATUS "🚀 Сборка основного сервера: ${TARGET_NAME}")
endif()

set(HEADERS
    ${BACKEND_SRC_DIR}/include/ecu_detector.h
    ${BACKEND_SRC_DIR}/include/obd_connector.h
    ${BACKEND_SRC_DIR}/include/serial_port.h
    ${BACKEND_SRC_DIR}/include/database.h
    ${BACKEND_SRC_DIR}/include/pch.h
)

# Создание исполняемого файла
add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

# Добавление include директорий
target_include_directories(${TARGET_NAME} PRIVATE
    ${BACKEND_SRC_DIR}/include
    ${SQLITE3_INCLUDE_DIR}
)

# Подключаем SQLite3
if(EXISTS "${SQLITE3_INCLUDE_DIR}/sqlite3.h" AND EXISTS "${SQLITE3_LIBRARY}")
    target_link_libraries(${TARGET_NAME} PRIVATE "${SQLITE3_LIBRARY}")
    message(STATUS "✅ SQLite3 подключен: ${SQLITE3_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "❌ SQLite3 не найден! Скопируйте файлы в:\n"
            "  ${SQLITE3_INCLUDE_DIR}/sqlite3.h\n"
            "  ${SQLITE3_LIBRARY}")
endif()

# Поиск Threads
find_package(Threads REQUIRED)
target_link_libraries(${TARGET_NAME} PRIVATE Threads::Threads)

# Платформозависимые настройки
if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE 
        ws2_32
        winmm
        iphlpapi
        crypt32
        bcrypt
        user32
        advapi32
        shell32
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        -DWINDOWS 
        -D_CRT_SECURE_NO_WARNINGS 
        -DNOMINMAX
        -DWIN32_LEAN_AND_MEAN
        -D_WINSOCK_DEPRECATED_NO_WARNINGS
    )
    
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W3 /EHsc /utf-8)
        # Отключаем лишние предупреждения
        target_compile_options(${TARGET_NAME} PRIVATE
            /wd4251 /wd4275 /wd4996 /wd4100 /wd4065 /wd4244
            /wd4267 /wd4018 /wd4267 /wd4566 /wd26439 /wd26478
            /wd4005  # Отключаем предупреждение о переопределении макросов
        )
    endif()
else()
    target_link_libraries(${TARGET_NAME} PRIVATE pthread rt dl)
    target_compile_definitions(${TARGET_NAME} PRIVATE -DLINUX)
endif()

# Информация о сборке
message(STATUS "=========================================================")
message(STATUS "✅ Проект: ECU Diagnostic System")
message(STATUS "✅ Версия: ${PROJECT_VERSION}")
message(STATUS "✅ Платформа: ${CMAKE_SYSTEM_NAME}")
message(STATUS "✅ Цель: ${TARGET_NAME}")
message(STATUS "✅ Исходные файлы: ${SOURCES}")
message(STATUS "✅ SQLite3: ${SQLITE3_INCLUDE_DIR}")
message(STATUS "=========================================================")