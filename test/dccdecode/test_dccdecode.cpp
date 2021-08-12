#include <dccdecode.h>
#include <unity.h>

void testInitial() {
    TEST_ASSERT_FALSE(dccdecode::hasNewMessage());
}

void writeDccByte(uint8_t aByte) {
    // Separator
    dccdecode::receivedBit(false);

    // Data
    for (int i = 7; i >= 0; i--) {
        dccdecode::receivedBit(aByte & (1 << i));
    }
}

void writePreamble(int size=12) {
    for (int i = 0; i < size; i++) {
        dccdecode::receivedBit(true);
    }
}

void writeTerminator() {
    dccdecode::receivedBit(true);
}

void testShortPreamble() {
    writePreamble(5);
    writeDccByte(0xFF);
    writeDccByte(0x00);
    writeDccByte(0xFF);
    writeTerminator();

    TEST_ASSERT_FALSE(dccdecode::hasNewMessage());
}

void testInvalidXor() {
    writePreamble();
    writeDccByte(0xFF);
    writeDccByte(0x00);
    writeDccByte(0xFE);
    writeTerminator();

    TEST_ASSERT_FALSE(dccdecode::hasNewMessage());
}

void testOverlyLongMessage() {
    writePreamble();
    for (int i = 0; i < 100; i++)
        writeDccByte(0x00);
    writeTerminator();

    TEST_ASSERT_FALSE(dccdecode::hasNewMessage());
}

void testReceiveMessage() {
    writePreamble();
    writeDccByte(0xF0);
    writeDccByte(0x0F);
    writeDccByte(0xFF);
    writeTerminator();

    TEST_ASSERT(dccdecode::hasNewMessage());
    TEST_ASSERT_EQUAL_MESSAGE(dccdecode::message.length, 3, "Message length");
    const uint8_t expected[] = { 0xF0, 0x0F, 0xFF };
    TEST_ASSERT_EQUAL_CHAR_ARRAY_MESSAGE(expected, dccdecode::message.data, sizeof(expected), "Message data");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(testInitial);
    RUN_TEST(testShortPreamble);
    RUN_TEST(testInvalidXor);
    RUN_TEST(testOverlyLongMessage);
    RUN_TEST(testReceiveMessage);
    UNITY_END();
    return 0;
}