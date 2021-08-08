#include <stdint.h>

/*!
 * Reading DCC data.
 * On ATTiny85, this uses:
 * - Int0 and PB2 for input
 * - Timer0 for reading the time
 */

// Stores the DCC message with length and data.
struct DccMessage {
  uint8_t length = 0;
  uint8_t data[10];

  bool isGeneralReset() const volatile {
    return length == 3 && data[0] == 0 && data[1] == 0;
  }
  // This is only a programming message immediately after a general reset or
  // other programming message.
  // Otherwiste it may be a locomotive message.
  bool isPossiblyProgramming() const volatile {
    return (data[0] & 0xF0) == 0x70;
  }

  bool isAccessoryMessage() const volatile {
    return length >= 3 && (data[0] & 0xC0) == 0x80;
  }
  bool isBasicAccessoryMessage() const volatile {
    return isAccessoryMessage() && (data[1] & 0x80) == 0x80;
  }
  uint16_t getAccessoryOutputAddress() const volatile {
    // Address format is weird. See RCN213.
    uint16_t address = (data[0] & 0x3F) | (0x7 & ~((data[1] & 0x70) >> 4));
    uint8_t port = (data[1] & 0x6) >> 1;
    return (address << 2 | port) - 3;
  }

  struct AddressData {
    uint16_t address; // If 0 then invalid
    uint8_t commandLength;
    const volatile uint8_t *commandData;
  };

  /**
   * Checks whether this is a locomotive message and if yes returns the address and
   * where to continue parsing.
   */
  AddressData parseLocomotiveMessage() const volatile {
    if (length >= 2 && (data[0] & 0x80) == 0) {
      // Short address
      return AddressData{
        /* .address = */ data[0],
        /* .commandLength = */ uint8_t(length - 1),
        /* .commandData = */ &data[1]
      };
    } else if (length >= 3 && (data[0] & 0xC0) == 0x80) {
      // Long address
      return AddressData{
        /* .address = */ data[1] | (uint16_t(data[0] & 0x3F) << 8),
        /* .commandLength = */ uint8_t(length - 2),
        /* .commandData = */ &data[2]
      };
    } else {
      return AddressData{ 0, 0, 0 };
    }
  }
};
// The current DCC message.
// This gets filled by interrupts; once its done, the message number (below) is increased by
// one. The rest of the code then has until the end of the next preamble to read it, before it
// starts getting overwritten again.
// A safer double-buffer technique is possible but pointless.
extern volatile DccMessage dccMessage;

// Called in setup the pin mode and interrupt
void setupDccInt0PB2();

// Called in setup to prepare the Timer0 used for DCC reading.
void setupDccTimer0();

// Returns whether a new DCC message has been received since the last time this function was called.
bool hasNewDccMessage();