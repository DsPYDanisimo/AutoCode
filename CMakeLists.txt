cmake_minimum_required(VERSION 3.16)
project(ECU_Diagnostic_System VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Определение путей
set(BACKEND_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/back)

# Опции сборки
option(BUILD_TEST  "Собрать тестовую программу (ручная)" OFF)
option(BUILD_GTEST "Собрать Google Test тесты"           OFF)

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
    # Сборка основного сервера
    set(SOURCES
        ${BACKEND_SRC_DIR}/src/action.cpp
        ${BACKEND_SRC_DIR}/src/ecu_protocols.cpp
        ${BACKEND_SRC_DIR}/src/obd_connector.cpp
        ${BACKEND_SRC_DIR}/src/serial_port.cpp
        ${BACKEND_SRC_DIR}/src/database.cpp
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

# ─── Google Test ─────────────────────────────────────────────────────────────
if(BUILD_GTEST)
    message(STATUS "⚙️ Подключение Google Test через FetchContent...")

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    # Не переопределяем настройки компилятора родительского проекта на Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # Используем /MD (Release CRT) чтобы совпадать с sqlite3.dll (Release CRT)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)
    endif()
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # Статическая сборка SQLite3 из амальгамации — без зависимости от DLL
    set(SQLITE3_AMALGAMATION "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3/sqlite3.c")
    if(EXISTS "${SQLITE3_AMALGAMATION}")
        add_library(sqlite3_static STATIC "${SQLITE3_AMALGAMATION}")
        target_include_directories(sqlite3_static PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3")
        if(MSVC)
            target_compile_options(sqlite3_static PRIVATE /utf-8 /wd4996)
        endif()
        set(SQLITE3_TEST_LINK sqlite3_static)
    else()
        set(SQLITE3_TEST_LINK "${SQLITE3_LIBRARY}")
    endif()

    set(TEST_SOURCES
        ${BACKEND_SRC_DIR}/tests/test_structs.cpp
        ${BACKEND_SRC_DIR}/tests/test_obd_utils.cpp
        ${BACKEND_SRC_DIR}/tests/test_database.cpp
        ${BACKEND_SRC_DIR}/src/database.cpp
        ${BACKEND_SRC_DIR}/src/obd_connector.cpp
        ${BACKEND_SRC_DIR}/src/serial_port.cpp
    )

    add_executable(ecu_tests ${TEST_SOURCES})

    target_include_directories(ecu_tests PRIVATE
        ${BACKEND_SRC_DIR}/include
        ${SQLITE3_INCLUDE_DIR}
    )

    target_link_libraries(ecu_tests PRIVATE
        GTest::gtest_main
        ${SQLITE3_TEST_LINK}
        Threads::Threads
    )

    if(WIN32)
        target_link_libraries(ecu_tests PRIVATE
            ws2_32 winmm iphlpapi crypt32 bcrypt user32 advapi32 shell32
        )
        target_compile_definitions(ecu_tests PRIVATE
            WINDOWS _CRT_SECURE_NO_WARNINGS NOMINMAX
            WIN32_LEAN_AND_MEAN _WINSOCK_DEPRECATED_NO_WARNINGS
        )
        if(MSVC)
            target_compile_options(ecu_tests PRIVATE
                /W3 /EHsc /utf-8
                /wd4251 /wd4275 /wd4996 /wd4100 /wd4065
                /wd4244 /wd4267 /wd4018 /wd4566 /wd4005 /wd4828
            )
        endif()
    else()
        target_link_libraries(ecu_tests PRIVATE pthread rt dl)
        target_compile_definitions(ecu_tests PRIVATE LINUX)
    endif()

    # add_test регистрирует исполняемый файл в CTest напрямую —
    # работает без запуска бинаря во время конфигурации (в отличие от gtest_discover_tests)
    add_test(NAME ecu_tests COMMAND ecu_tests)

    message(STATUS "Google Test target: ecu_tests")
endif()