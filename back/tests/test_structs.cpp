// test_structs.cpp — тесты структур данных (без железа, чистая логика)
#include <gtest/gtest.h>
#include <set>

#include "obd_connector.h"   // KWP2000Packet, UDSPacket, OBDParameter, KWP2000Parameter
#include "ecu_detector.h"    // CANMessage, ECUConnection, DTCCode, PortInfo,
                             // UDSServices, UDSParameter, Protocol

// =============================================================================
// KWP2000Packet
// =============================================================================

class KWP2000PacketTest : public ::testing::Test {};

TEST_F(KWP2000PacketTest, DefaultConstructorZeroesAllFields) {
    KWP2000Packet pkt;
    EXPECT_EQ(pkt.header,      0);
    EXPECT_EQ(pkt.target_addr, 0);
    EXPECT_EQ(pkt.source_addr, 0);
    EXPECT_EQ(pkt.service_id,  0);
    EXPECT_EQ(pkt.checksum,    0);
    EXPECT_TRUE(pkt.data.empty());
}

TEST_F(KWP2000PacketTest, ToBytesBasicOrder) {
    KWP2000Packet pkt;
    pkt.header      = 0x80;
    pkt.target_addr = 0x10;
    pkt.source_addr = 0xF1;
    pkt.service_id  = 0x21;
    pkt.checksum    = 0x42;

    auto bytes = pkt.to_bytes();
    ASSERT_EQ(bytes.size(), 5u);
    EXPECT_EQ(bytes[0], 0x80);
    EXPECT_EQ(bytes[1], 0x10);
    EXPECT_EQ(bytes[2], 0xF1);
    EXPECT_EQ(bytes[3], 0x21);
    EXPECT_EQ(bytes[4], 0x42);
}

TEST_F(KWP2000PacketTest, ToBytesIncludesDataPayload) {
    KWP2000Packet pkt;
    pkt.header      = 0x80;
    pkt.target_addr = 0x10;
    pkt.source_addr = 0xF1;
    pkt.service_id  = 0x21;
    pkt.data        = {0xAA, 0xBB, 0xCC};
    pkt.checksum    = 0x00;

    auto bytes = pkt.to_bytes();
    // header + target + source + service_id + 3 data + checksum = 8
    ASSERT_EQ(bytes.size(), 8u);
    EXPECT_EQ(bytes[4], 0xAA);
    EXPECT_EQ(bytes[5], 0xBB);
    EXPECT_EQ(bytes[6], 0xCC); // data[2]
    EXPECT_EQ(bytes[7], 0x00); // checksum is last
}

TEST_F(KWP2000PacketTest, ToBytesEmptyDataYieldsFiveBytes) {
    KWP2000Packet pkt;
    pkt.header = pkt.target_addr = pkt.source_addr = pkt.service_id = pkt.checksum = 0x01;
    EXPECT_EQ(pkt.to_bytes().size(), 5u);
}

TEST_F(KWP2000PacketTest, IsValidCorrectChecksumNoData) {
    KWP2000Packet pkt;
    pkt.header      = 0x80;
    pkt.target_addr = 0x10;
    pkt.source_addr = 0xF1;
    pkt.service_id  = 0x21;
    // checksum = (0x80 + 0x10 + 0xF1 + 0x21) truncated to uint8_t
    pkt.checksum    = static_cast<uint8_t>(0x80u + 0x10u + 0xF1u + 0x21u);

    EXPECT_TRUE(pkt.is_valid());
}

TEST_F(KWP2000PacketTest, IsValidWrongChecksumFails) {
    KWP2000Packet pkt;
    pkt.header      = 0x80;
    pkt.target_addr = 0x10;
    pkt.source_addr = 0xF1;
    pkt.service_id  = 0x21;
    pkt.checksum    = 0x00; // deliberately wrong

    EXPECT_FALSE(pkt.is_valid());
}

TEST_F(KWP2000PacketTest, IsValidCorrectChecksumWithData) {
    KWP2000Packet pkt;
    pkt.header      = 0x80;
    pkt.target_addr = 0x10;
    pkt.source_addr = 0xF1;
    pkt.service_id  = 0x21;
    pkt.data        = {0x01, 0x02};
    uint8_t cs = static_cast<uint8_t>(0x80u + 0x10u + 0xF1u + 0x21u + 0x01u + 0x02u);
    pkt.checksum    = cs;

    EXPECT_TRUE(pkt.is_valid());
}

TEST_F(KWP2000PacketTest, IsValidZeroPacket) {
    KWP2000Packet pkt; // все поля = 0, checksum = 0
    // sum = 0 → checksum must be 0
    EXPECT_TRUE(pkt.is_valid());
}

// =============================================================================
// UDSPacket
// =============================================================================

class UDSPacketTest : public ::testing::Test {};

TEST_F(UDSPacketTest, DefaultConstructor) {
    UDSPacket pkt;
    EXPECT_EQ(pkt.service_id, 0);
    EXPECT_TRUE(pkt.data.empty());
}

TEST_F(UDSPacketTest, HasNegativeResponseFalseForPositiveResponse) {
    UDSPacket pkt;
    pkt.service_id = 0x62; // ответ на ReadDataByIdentifier (0x22)
    EXPECT_FALSE(pkt.has_negative_response());
}

TEST_F(UDSPacketTest, HasNegativeResponseFalseForZero) {
    UDSPacket pkt;
    pkt.service_id = 0x00;
    EXPECT_FALSE(pkt.has_negative_response());
}

TEST_F(UDSPacketTest, HasNegativeResponseFalseFor0x7E) {
    UDSPacket pkt;
    pkt.service_id = 0x7E;
    EXPECT_FALSE(pkt.has_negative_response());
}

TEST_F(UDSPacketTest, HasNegativeResponseTrueAt0x7F) {
    UDSPacket pkt;
    pkt.service_id = 0x7F;
    EXPECT_TRUE(pkt.has_negative_response());
}

TEST_F(UDSPacketTest, HasNegativeResponseTrueAbove0x7F) {
    UDSPacket pkt;
    pkt.service_id = 0xFF;
    EXPECT_TRUE(pkt.has_negative_response());
}

TEST_F(UDSPacketTest, GetNegativeResponseCodeReturnsFirstDataByte) {
    UDSPacket pkt;
    pkt.service_id = 0x7F;
    pkt.data = {0x22, 0x31}; // requestOutOfRange
    EXPECT_EQ(pkt.get_negative_response_code(), 0x22);
}

TEST_F(UDSPacketTest, GetNegativeResponseCodeReturnsZeroWhenDataEmpty) {
    UDSPacket pkt;
    pkt.service_id = 0x7F;
    // data is empty
    EXPECT_EQ(pkt.get_negative_response_code(), 0);
}

TEST_F(UDSPacketTest, GetNegativeResponseCodeZeroForPositiveResponse) {
    UDSPacket pkt;
    pkt.service_id = 0x62;
    pkt.data       = {0xAA};
    EXPECT_EQ(pkt.get_negative_response_code(), 0);
}

// =============================================================================
// CANMessage
// =============================================================================

class CANMessageTest : public ::testing::Test {};

TEST_F(CANMessageTest, DefaultConstructorValues) {
    CANMessage msg;
    EXPECT_EQ(msg.id, 0u);
    EXPECT_FALSE(msg.is_extended);
    EXPECT_TRUE(msg.data.empty());
}

TEST_F(CANMessageTest, ConstructorWithIdAndData) {
    std::vector<uint8_t> payload = {0x02, 0x01, 0x0C};
    CANMessage msg(0x7DF, payload);
    EXPECT_EQ(msg.id, 0x7DFu);
    EXPECT_FALSE(msg.is_extended);
    EXPECT_EQ(msg.data, payload);
}

TEST_F(CANMessageTest, ExtendedFrameFlag) {
    std::vector<uint8_t> payload = {0xFF};
    CANMessage msg(0x18DB33F1, payload, true);
    EXPECT_EQ(msg.id, 0x18DB33F1u);
    EXPECT_TRUE(msg.is_extended);
}

TEST_F(CANMessageTest, StandardFrameDefaultIsNotExtended) {
    CANMessage msg(0x7E0, {0x01});
    EXPECT_FALSE(msg.is_extended);
}

TEST_F(CANMessageTest, DataContentsPreserved) {
    std::vector<uint8_t> payload = {0x00, 0x01, 0xFE, 0xFF};
    CANMessage msg(0x7DF, payload);
    ASSERT_EQ(msg.data.size(), 4u);
    EXPECT_EQ(msg.data[0], 0x00);
    EXPECT_EQ(msg.data[1], 0x01);
    EXPECT_EQ(msg.data[2], 0xFE);
    EXPECT_EQ(msg.data[3], 0xFF);
}

TEST_F(CANMessageTest, TimestampIsSet) {
    auto before = std::chrono::system_clock::now();
    CANMessage msg(0x7DF, {0x01});
    auto after = std::chrono::system_clock::now();
    EXPECT_GE(msg.timestamp, before);
    EXPECT_LE(msg.timestamp, after);
}

// =============================================================================
// ECUConnection
// =============================================================================

TEST(ECUConnectionTest, DefaultValues) {
    ECUConnection conn;
    EXPECT_FALSE(conn.is_connected);
    EXPECT_EQ(conn.signal_quality, 0);
    EXPECT_TRUE(conn.protocol.empty());
    EXPECT_TRUE(conn.port.empty());
    EXPECT_TRUE(conn.vin.empty());
    EXPECT_TRUE(conn.supported_pids.empty());
    EXPECT_TRUE(conn.diagnostic_data.empty());
}

TEST(ECUConnectionTest, FieldsAreAssignable) {
    ECUConnection conn;
    conn.is_connected   = true;
    conn.protocol       = "OBD-II";
    conn.port           = "COM3";
    conn.signal_quality = 85;
    conn.vin            = "1HGBH41JXMN109186";

    EXPECT_TRUE(conn.is_connected);
    EXPECT_EQ(conn.protocol, "OBD-II");
    EXPECT_EQ(conn.signal_quality, 85);
    EXPECT_EQ(conn.vin, "1HGBH41JXMN109186");
}

// =============================================================================
// DTCCode
// =============================================================================

TEST(DTCCodeTest, DefaultSeverityIsZero) {
    DTCCode dtc;
    EXPECT_EQ(dtc.severity, 0);
}

TEST(DTCCodeTest, DefaultFieldsAreEmpty) {
    DTCCode dtc;
    EXPECT_TRUE(dtc.code.empty());
    EXPECT_TRUE(dtc.description.empty());
    EXPECT_TRUE(dtc.status.empty());
    EXPECT_TRUE(dtc.possible_causes.empty());
    EXPECT_TRUE(dtc.environment_data.empty());
}

TEST(DTCCodeTest, FieldsAreAssignable) {
    DTCCode dtc;
    dtc.code        = "P0300";
    dtc.description = "Random/Multiple Cylinder Misfire";
    dtc.status      = "active";
    dtc.severity    = 4;

    EXPECT_EQ(dtc.code, "P0300");
    EXPECT_EQ(dtc.severity, 4);
    EXPECT_EQ(dtc.status, "active");
}

// =============================================================================
// PortInfo
// =============================================================================

TEST(PortInfoTest, DefaultValues) {
    PortInfo pi;
    EXPECT_FALSE(pi.is_available);
    EXPECT_EQ(pi.baud_rate, 115200);
    EXPECT_EQ(pi.port, 0);
    EXPECT_TRUE(pi.name.empty());
    EXPECT_TRUE(pi.type.empty());
    EXPECT_TRUE(pi.host.empty());
}

TEST(PortInfoTest, FieldsAreAssignable) {
    PortInfo pi;
    pi.name         = "COM3";
    pi.type         = "serial";
    pi.is_available = true;
    pi.baud_rate    = 9600;

    EXPECT_EQ(pi.name, "COM3");
    EXPECT_TRUE(pi.is_available);
    EXPECT_EQ(pi.baud_rate, 9600);
}

// =============================================================================
// UDSServices constants
// =============================================================================

class UDSServicesTest : public ::testing::Test {};

TEST_F(UDSServicesTest, DiagnosticSessionControl) {
    EXPECT_EQ(UDSServices::DIAGNOSTIC_SESSION_CONTROL, 0x10u);
}

TEST_F(UDSServicesTest, EcuReset) {
    EXPECT_EQ(UDSServices::ECU_RESET, 0x11u);
}

TEST_F(UDSServicesTest, SecurityAccess) {
    EXPECT_EQ(UDSServices::SECURITY_ACCESS, 0x27u);
}

TEST_F(UDSServicesTest, TesterPresent) {
    EXPECT_EQ(UDSServices::TESTER_PRESENT, 0x3Eu);
}

TEST_F(UDSServicesTest, ReadDataByIdentifier) {
    EXPECT_EQ(UDSServices::READ_DATA_BY_IDENTIFIER, 0x22u);
}

TEST_F(UDSServicesTest, WriteDataByIdentifier) {
    EXPECT_EQ(UDSServices::WRITE_DATA_BY_IDENTIFIER, 0x2Eu);
}

TEST_F(UDSServicesTest, ClearDiagnosticInfo) {
    EXPECT_EQ(UDSServices::CLEAR_DIAGNOSTIC_INFO, 0x14u);
}

TEST_F(UDSServicesTest, ReadDtcInfo) {
    EXPECT_EQ(UDSServices::READ_DTC_INFO, 0x19u);
}

TEST_F(UDSServicesTest, RoutineControl) {
    EXPECT_EQ(UDSServices::ROUTINE_CONTROL, 0x31u);
}

TEST_F(UDSServicesTest, RequestDownload) {
    EXPECT_EQ(UDSServices::REQUEST_DOWNLOAD, 0x34u);
}

TEST_F(UDSServicesTest, TransferData) {
    EXPECT_EQ(UDSServices::TRANSFER_DATA, 0x36u);
}

TEST_F(UDSServicesTest, RequestTransferExit) {
    EXPECT_EQ(UDSServices::REQUEST_TRANSFER_EXIT, 0x37u);
}

TEST_F(UDSServicesTest, AllConstantsAreUnique) {
    std::set<uint8_t> vals = {
        UDSServices::DIAGNOSTIC_SESSION_CONTROL,
        UDSServices::ECU_RESET,
        UDSServices::SECURITY_ACCESS,
        UDSServices::TESTER_PRESENT,
        UDSServices::READ_DATA_BY_IDENTIFIER,
        UDSServices::WRITE_DATA_BY_IDENTIFIER,
        UDSServices::CLEAR_DIAGNOSTIC_INFO,
        UDSServices::READ_DTC_INFO,
        UDSServices::ROUTINE_CONTROL,
        UDSServices::REQUEST_DOWNLOAD,
        UDSServices::TRANSFER_DATA,
        UDSServices::REQUEST_TRANSFER_EXIT,
    };
    EXPECT_EQ(vals.size(), 12u);
}

// =============================================================================
// Protocol enum
// =============================================================================

TEST(ProtocolEnumTest, NoneIsDistinctFromOthers) {
    EXPECT_NE(Protocol::NONE,    Protocol::OBD2);
    EXPECT_NE(Protocol::NONE,    Protocol::KWP2000);
    EXPECT_NE(Protocol::NONE,    Protocol::CAN);
    EXPECT_NE(Protocol::NONE,    Protocol::UDS);
}

TEST(ProtocolEnumTest, AllValuesAreDistinct) {
    std::set<int> vals = {
        static_cast<int>(Protocol::NONE),
        static_cast<int>(Protocol::OBD2),
        static_cast<int>(Protocol::KWP2000),
        static_cast<int>(Protocol::CAN),
        static_cast<int>(Protocol::UDS),
    };
    EXPECT_EQ(vals.size(), 5u);
}
