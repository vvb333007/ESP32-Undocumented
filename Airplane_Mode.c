//
// Example demonstrating undocumented ESP32-S3 capabilities.
// Enables and disables "Airplane Mode" by shutting down the RF PHY
// (both TX and RX are disabled at the physical layer).
//
// The MAC layer and higher-level software remain unaware that RF
// is disabled, which makes this approach convenient for experiments.
//
// When RF is re-enabled, WiFi continues operating as if nothing
// happened.
//
#include <Arduino.h>

// Write a value to a peripheral register
//
static inline void reg_store(uintptr_t const reg, uint32_t const val) {
  *(volatile uint32_t *)reg = val;
}

// Read a peripheral register
//
static inline uint32_t reg_load(uintptr_t const reg) {
  asm volatile ("memw" ::: "memory");  // prevent reordering. 
  return *(volatile uint32_t *)reg;
}

#ifdef __cplusplus
extern "C" {
#endif

// Uncomment this declaration if the compiler complains about ets_delay_us()
//extern void ets_delay_us(uint32_t usec);

#ifdef __cplusplus
};
#endif

// Disable the ESP32-S3 RF subsystem
// Magic constant 0
//
static void esp32s3_rf_off(bool arg) {

  uint32_t val;

  val = reg_load(0x60006110) & 0xfffff0ffUL;

  if (arg) {

    reg_store(0x60006110, val | 0x800);
    ets_delay_us(1);
    reg_store(0x60006110, val | 0xa00);

  } else {

    reg_store(0x60006110, val | 0x200);
    ets_delay_us(1);
    reg_store(0x60006110, val | 0x000);

  }

  ets_delay_us(1);
}

// Enable the ESP32-S3 RF subsystem again
// Magic constant 1
//
static void esp32s3_rf_on() {

  reg_store(0x60006110, 0x1000050);
}

// Enable Airplane mode (hardware RX/TX off)
//
void airplane_mode_on() {

  esp32s3_rf_off(0);
  ets_delay_us(1000);
  esp32s3_rf_off(1);

}

// Disable Airplane mode, return to normal RX/TX
//
void airplane_mode_off() {

  esp32s3_rf_on();
}
