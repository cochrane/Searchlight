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
};
// The current DCC message.
// This gets filled by interrupts; once its done, the message number (below) is increased by
// one. The rest of the code then has until the end of the next preamble to read it, before it
// starts getting overwritten again.
// A safer double-buffer technique is possible but pointless.
extern volatile DccMessage dccMessage;
// The counter for messages; increases by one every time a new complete message has been read.
extern volatile uint8_t currentMessageNumber;

// Called in setup the pin mode and interrupt
void setupDccInt0PB2();

// Called in setup to prepare the Timer0 used for DCC reading.
void setupDccTimer0();