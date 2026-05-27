// Undocumented timers: ESP32-S3 Microsecond Counters: 32/64-bit, read/write
// 4 full 64bit independent usec counters (read/write) and one 32 bit freerunning counter
//
// Two ways to get a microsecond timestamp:
//
// 1. Very fast, but returns only the lower 32 bits
// 2. Full 64-bit timestamp, slightly slower, 4 timers, read/write access
//
// For simplicity, this example uses the Arduino framework.
//
// Setting the microsecond counters MAY interfere with WiFi TSF logic.
// Or maybe not — I haven't tested it.
//
// WiFi does not need to be initialized to use these counters.
//
// vvb333007@gmail.com
// vvb@nym.hush.com
//
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

// Command Register:
//
#define WIFI_TSF_CMD_REG (volatile uint32_t *)((uintptr_t)0x6003500c)
#  define WIFI_TSFC_LATCH 0x01 // Latch current counter value into MMIO regs 0x18 and 0x1C
#  define WIFI_TSFC_SET   0x10 // Set current counter value from regs 0x10 and 0x14

// Counter Enable Registers (4 registers)
//
#define WIFI_TSF_EN_REG(_Counter) (volatile uint32_t *)((uintptr_t)(0x60035028 + _Counter * 0x0c))
#  define WIFI_TSFE_ENABLE 0x98000000  // Enable flags (TODO: leave only required flags)
 
// Latched 64-bit counter value.
// After issuing the LATCH command, these registers contain
// a snapshot of the current microsecond counter.
//
#define WIFI_TSF_HIGH_REG (volatile uint32_t *)((uintptr_t)0x6003501c)
#define WIFI_TSF_LOW_REG  (volatile uint32_t *)((uintptr_t)0x60035018)

// Setter registers.
// Values written here are used when updating
// the current microsecond counter.
//
#define WIFI_TSF_SET_HIGH_REG (volatile uint32_t *)((uintptr_t)0x60035014)
#define WIFI_TSF_SET_LOW_REG  (volatile uint32_t *)((uintptr_t)0x60035010)

// Free-running 32-bit microsecond counter.
// The simplest and fastest option.
//
#define WIFI_MICROS_REG (volatile uint32_t *)((uintptr_t)0x60035000)


// Fastest possible micros() implementation
//
static inline uint32_t micros32() {
  return *WIFI_MICROS_REG;
}

// Full 64-bit microsecond timestamp.
//
// Create a snapshot, read two 32-bit values,
// and combine them into a 64-bit result.
//
uint64_t micros64(uint8_t counter) {

  uint32_t low = 0, high = 0;

  if (counter < 4) {

    // Create snapshot
    *WIFI_TSF_CMD_REG |= (WIFI_TSFC_LATCH << counter);

    // Read latched values
    low = *WIFI_TSF_LOW_REG;
    high = *WIFI_TSF_HIGH_REG;

    // Release latch
    *WIFI_TSF_CMD_REG &= ~(WIFI_TSFC_LATCH  << counter);
  }

  // Build 64-bit result
  return (((uint64_t)high) << 32) | (uint64_t)low;
}

// Set the current microsecond counter value
//
void setmicros64(uint8_t counter, uint64_t val) {

  if (counter < 4) {
    *WIFI_TSF_SET_LOW_REG = (uint32_t)(val & 0xffffffffUL);
    *WIFI_TSF_SET_HIGH_REG = (uint32_t)(val >> 32);

    *WIFI_TSF_CMD_REG |= (WIFI_TSFC_SET  << counter);
  }
}

// Enable/Disable counters
void micros64en(uint8_t counter, bool en) {

  if (counter < 4) {
    if (en) {
      *WIFI_TSF_EN_REG(counter) |= WIFI_TSFE_ENABLE;
    } else {
      *WIFI_TSF_EN_REG(counter) &= ~WIFI_TSFE_ENABLE;
    }
  }
}


// Demo
//
void setup() {

  Serial.begin(115200);
  delay(300);
  Serial.printf("\r\n\r\nESP32-S3 microsecond counters demo:\r\n");
  //micros64en(0, true); // Works on its own from the start. May be it is a good idea to call enable on this one too
  micros64en(1, true);
  micros64en(2, true);
  micros64en(3, true);
}

// 1. Display both 64-bit and 32-bit counters once per second
//    for a 10-second interval
//
// 2. Reset the 64-bit counter to zero
//
// 3. Repeat
//
void loop() {

  for (int i = 0; i < 10; i++) {
    Serial.printf("micros64 = %llu / %llu / %llu / %llu\r\n", micros64(0), micros64(1), micros64(2), micros64(3) );
    delay(1000);
  }

  Serial.printf("Resetting 64-bit counters 1 and 3 to zero, keeping 32-bit running...\r\n");

  setmicros64(1, 0);
  setmicros64(3, 0);
}
