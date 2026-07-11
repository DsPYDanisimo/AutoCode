// test_obd_utils.cpp — тесты статических утилит OBDConnector (без железа)
#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>

#include "obd_connector.h"

// =============================================================================
// OBDConnector::bytes_to_hex()
// =============================================================================
// Реализация: stringstream с hex/uppercase/setw(2)/setfill('0')
// Пробелы между байтами НЕ добавляются → "410C1AF0"

class BytesToHexTest : public ::testing::Test {};

TEST_F(BytesToHexTest, EmptyInputReturnsEmptyString) {
    EXPECT_EQ(OBDConnector::bytes_to_hex({}), "");
}

TEST_F(BytesToHexTest, SingleByteZero) {
    EXPECT_EQ(OBDConnector::bytes_to_hex({0x00}), "00");
}

TEST_F(BytesToHexTest, SingleByteLowNibbleOnly) {
    EXPECT_EQ(OBDConnector::bytes_to_hex({0x0A}), "0A");
}

TEST_F(BytesToHexTest, SingleByteMaxFF) {
    EXPECT_EQ(OBDConnector::bytes_to_hex({0xFF}), "FF");
}

TEST_F(BytesToHexTest, MultipleBytes) {
    EXPECT_EQ(OBDConnector::bytes_to_hex({0x41, 0x0C, 0x1A, 0xF0}), "410C1AF0");
}

TEST_F(BytesToHexTest, OutputIsUpperCase) {
    auto result = OBDConnector::bytes_to_hex({0xAB, 0xCD, 0xEF});
    // Все буквы должны быть заглавными
    for (char c : result) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            EXPECT_EQ(c, std::toupper(static_cast<unsigned char>(c)))
                << "Lowercase letter found: " << c;
        }
    }
}

TEST_F(BytesToHexTest, LengthIsExactlyTwoCharsPerByte) {
    std::vector<uint8_t> bytes = {0x00, 0x0F, 0xF0, 0xFF};
    auto result = OBDConnector::bytes_to_hex(bytes);
    EXPECT_EQ(result.size(), bytes.size() * 2);
}

TEST_F(BytesToHexTest, OBD2ResponseRpmPid) {
    // PID 0x41 0x0C AA BB → RPM = (AA*256+BB)/4
    EXPECT_EQ(OBDConnector::bytes_to_hex({0x41, 0x0C}), "410C");
}

TEST_F(BytesToHexTest, AllZeroBytes) {
    EXPECT_EQ(OBDConnector::bytes_to_hex({0x00, 0x00, 0x00}), "000000");
}

TEST_F(BytesToHexTest, AllFFBytes) {
    EXPECT_EQ(OBDConnector::bytes_to_hex({0xFF, 0xFF}), "FFFF");
}

// =============================================================================
// OBDConnector::hex_to_bytes()
// =============================================================================
// Реализация: читает по 2 символа за итерацию, без пробелов

class HexToBytesTest : public ::testing::Test {};

TEST_F(HexToBytesTest, EmptyStringReturnsEmptyVector) {
    EXPECT_TRUE(OBDConnector::hex_to_bytes("").empty());
}

TEST_F(HexToBytesTest, SingleByteZero) {
    auto result = OBDConnector::hex_to_bytes("00");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], 0x00);
}

TEST_F(HexToBytesTest, SingleByteLowNibble) {
    auto result = OBDConnector::hex_to_bytes("0A");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], 0x0A);
}

TEST_F(HexToBytesTest, SingleByteFF) {
    auto result = OBDConnector::hex_to_bytes("FF");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], 0xFF);
}

TEST_F(HexToBytesTest, MultipleBytes) {
    auto result = OBDConnector::hex_to_bytes("410C1AF0");
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], 0x41);
    EXPECT_EQ(result[1], 0x0C);
    EXPECT_EQ(result[2], 0x1A);
    EXPECT_EQ(result[3], 0xF0);
}

TEST_F(HexToBytesTest, LowercaseHex) {
    // hex_to_bytes должен принять строчные символы
    auto result = OBDConnector::hex_to_bytes("deadbeef");
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], 0xDE);
    EXPECT_EQ(result[1], 0xAD);
    EXPECT_EQ(result[2], 0xBE);
    EXPECT_EQ(result[3], 0xEF);
}

TEST_F(HexToBytesTest, AllZeroBytes) {
    auto result = OBDConnector::hex_to_bytes("000000");
    ASSERT_EQ(result.size(), 3u);
    for (auto b : result) EXPECT_EQ(b, 0x00);
}

TEST_F(HexToBytesTest, AllFFBytes) {
    auto result = OBDConnector::hex_to_bytes("FFFF");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], 0xFF);
    EXPECT_EQ(result[1], 0xFF);
}

// =============================================================================
// Round-trip: bytes_to_hex → hex_to_bytes
// =============================================================================

class HexRoundTripTest : public ::testing::Test {};

TEST_F(HexRoundTripTest, SingleByte) {
    std::vector<uint8_t> original = {0xAB};
    auto hex    = OBDConnector::bytes_to_hex(original);
    auto result = OBDConnector::hex_to_bytes(hex);
    EXPECT_EQ(result, original);
}

TEST_F(HexRoundTripTest, MultipleBytes) {
    std::vector<uint8_t> original = {0x41, 0x0C, 0x1A, 0xF0, 0x00, 0xFF};
    auto hex    = OBDConnector::bytes_to_hex(original);
    auto result = OBDConnector::hex_to_bytes(hex);
    EXPECT_EQ(result, original);
}

TEST_F(HexRoundTripTest, AllByteValues) {
    std::vector<uint8_t> original;
    for (int i = 0; i < 256; ++i) original.push_back(static_cast<uint8_t>(i));

    auto hex    = OBDConnector::bytes_to_hex(original);
    auto result = OBDConnector::hex_to_bytes(hex);
    EXPECT_EQ(result, original);
}

TEST_F(HexRoundTripTest, TypedOBDResponse) {
    // Типичный ответ ELM327 на PID 0x0C (RPM): 41 0C 1A F8 → (0x1A*256+0xF8)/4 = 1726 rpm
    std::vector<uint8_t> response = {0x41, 0x0C, 0x1A, 0xF8};
    auto hex    = OBDConnector::bytes_to_hex(response);
    auto parsed = OBDConnector::hex_to_bytes(hex);
    ASSERT_EQ(parsed.size(), 4u);
    double rpm = (static_cast<double>(parsed[2]) * 256.0 + parsed[3]) / 4.0;
    EXPECT_DOUBLE_EQ(rpm, (0x1A * 256.0 + 0xF8) / 4.0);
}

// =============================================================================
// OBDParameter struct
// =============================================================================

TEST(OBDParameterTest, DefaultConstructor) {
    OBDParameter p;
    EXPECT_EQ(p.min_value, 0.0);
    EXPECT_EQ(p.max_value, 0.0);
    EXPECT_TRUE(p.pid.empty());
    EXPECT_TRUE(p.name.empty());
}

TEST(OBDParameterTest, ParameterizedConstructor) {
    OBDParameter p("010C", "RPM", "rpm", 0.0, 16383.75, "(A*256+B)/4");
    EXPECT_EQ(p.pid,       "010C");
    EXPECT_EQ(p.name,      "RPM");
    EXPECT_EQ(p.unit,      "rpm");
    EXPECT_EQ(p.min_value, 0.0);
    EXPECT_DOUBLE_EQ(p.max_value, 16383.75);
    EXPECT_EQ(p.formula,   "(A*256+B)/4");
}

// =============================================================================
// KWP2000Parameter struct
// =============================================================================

TEST(KWP2000ParameterTest, DefaultConstructor) {
    KWP2000Parameter p;
    EXPECT_EQ(p.id,        0);
    EXPECT_EQ(p.min_value, 0.0f);
    EXPECT_EQ(p.max_value, 0.0f);
    EXPECT_TRUE(p.name.empty());
}
