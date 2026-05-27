// This code demonstrates a hidden ESP32-S3 peripheral: "Backup DMA engine"
//
// This peripheral is used to copy (backup) data from MMIO registers (either contiguous blocks
// or fragments) to SRAM and vice versa. Bypasses WCL.
//
// The code below was tested in Arduino framework on ESP32-S3 (USB-CDC mode, UART0 was not used)
// vvb@nym.hush.com

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// Import safe `printf` from S3 ROM
//
#ifdef __cplusplus
extern "C" {
#endif
extern int ets_printf(const char *, ... );
//extern esp_rom_printf(const char *, ... );
#ifdef __cplusplus
};
#endif


// BK DMA peripheral registers:
//
// BK DMA MMIO region starts at address 0x6001a000 and extends to at least 0x6001a028
// This region requires 4-byte aligned 32-bit access, so all registers must be accessed
// as full 32-bit words


// Main command/configuration register BKDMA_CMD_CFG_REG
//
#define BKDMA_CMD_CFG_REG           ((volatile uint32_t *)0x6001a000)
#   define BKDMA_CC_MODE_BITMAP     8              // Enable "bitmap" mode
#   define BKDMA_CC_CMD_START       0x20000000     // Start DMA engine
#   define BKDMA_CC_MODE_MMIO2RAM   0x40000000     // Set transfer direction
#   define BKDMA_CC_CMD_LATCH       0x80000000     // Probably: latch config into internal shadow registers

// Addresses: MMIO and SRAM addresses used for transfer
//
#define BKDMA_MMIO_ADDR_REG         ((volatile uint32_t *)0x6001a004) // 0x6xxxxxxx address
#define BKDMA_SRAM_ADDR_REG         ((volatile uint32_t *)0x6001a008) // Normal RAM address

// "Bitmap" access: this mode is not fully understood/tested and thus is omitted here.
//
// In short: 128 bits in 4 registers define which MMIO registers within a 4K window should be read.
// Each bit corresponds to one 32-bit word to copy: 128 bits x 32 bytes = 4K (one memory page).
// I did not test this mode.
//
#define BKDMA_BITMAP0_REG ((volatile uint32_t *)0x6001a00c) // bits 0..31
#define BKDMA_BITMAP1_REG ((volatile uint32_t *)0x6001a010) // bits 32..63
#define BKDMA_BITMAP2_REG ((volatile uint32_t *)0x6001a014) //
#define BKDMA_BITMAP3_REG ((volatile uint32_t *)0x6001a018) // bits ..127


// This register contains the IDLE bit
//
#define BKDMA_STATUS_REG ((volatile uint32_t *)0x6001a01c)
#   define BKDMA_S_IDLE 1 // LSB is IDLE bit: normally 1; becomes 0 when BUSY

// Command register #2: purpose is unclear, but acts like some sort of "enable"
// Automatically resets after operation is complete
//
#define BKDMA_CMD2_REG     ((volatile uint32_t *)0x6001a028)
#   define BKDMA_C2_EN     0x00000001

// This register is documented in the S3 TRM: System Configuration Register
// (TRM v1.7, Chapter "4.3.5.1 Module/Peripheral Address Mapping")
// Used to enable/disable clock for the backup DMA peripheral
//
#define SYSTEM_PERIP_CLK_EN1_REG  ((volatile uint32_t *)0x600c001c)
#   define SYSTEM_PERI_BACKUP_CLK_EN 0x00000001

// Transfer a memory block between MMIO register space and SRAM
//
// Example: copy 0x6003500 ... 0x6003510 to a buffer:
//    bkdma_exec((void *)0x6003500, my_buf, 4, true);
//
// Example: copy a buffer to 0x6003500 ... 0x6003510
//    bkdma_exec((void *)0x6003500, my_buf, 4, false);
//
// NOTE1: Does not raise exceptions or generate interrupts if `ram` address is invalid
// NOTE2: If `mmio` does not point to 0x6xxxxxxx region, this function effectively behaves like
//        a "special" memset(ram, 0, count * 4). It never generates exceptions and is not
//        visible to debuggers/watchpoints.
//
int bkdma_exec(void   *mmio,     // `mmio`     : MMIO address, e.g. 0x60035000
               void   *ram,      // `ram`      : Address in SRAM, e.g. static array address
               uint8_t count,    // `count`    : Number of 32-bit words to transfer
               bool    mmio2ram  // `mmio2ram` : true = MMIO-->SRAM, false = SRAM-->MMIO
              ) {

  uint32_t tmp;

  // All parameters must be valid: aligned addresses, non-zero count
  //
  if (mmio == NULL               ||
      ram == NULL                ||
      count == 0                 ||
      ((uintptr_t)mmio & 3) != 0 ||
      ((uintptr_t)ram & 3) != 0) {
      
    ets_printf("bkdma_exec(): bad count / bad address / bad alignment: %x, %x, %u\r\n",
               (unsigned int)mmio, (unsigned int)ram, count);
    return -1;
  }

  // Enable BK DMA clock
  *SYSTEM_PERIP_CLK_EN1_REG = *SYSTEM_PERIP_CLK_EN1_REG | SYSTEM_PERI_BACKUP_CLK_EN;

  // Configure "simple" mode
  *BKDMA_CMD_CFG_REG = *BKDMA_CMD_CFG_REG & ~BKDMA_CC_MODE_BITMAP;
  
  // Configure transfer direction (MMIO-->SRAM or SRAM-->MMIO)
  if (mmio2ram)
    tmp = *BKDMA_CMD_CFG_REG | BKDMA_CC_MODE_MMIO2RAM;
  else
    tmp = *BKDMA_CMD_CFG_REG & ~BKDMA_CC_MODE_MMIO2RAM;
  *BKDMA_CMD_CFG_REG = tmp;
    
  // Write MMIO address and SRAM address into registers
  *BKDMA_MMIO_ADDR_REG = (uintptr_t)mmio;
  *BKDMA_SRAM_ADDR_REG = (uintptr_t)ram;

  // Setup transaction size (in 32-bit words), 10-bit field --> up to ~4 KB range
  *BKDMA_CMD_CFG_REG = (*BKDMA_CMD_CFG_REG & 0xE007FFFF) | ((count << 19) & 0x1FF80000);

  // Prep & start:
  // Naming may be incorrect: START could be LATCH and vice versa.
  // I do not know which bit actually starts the DMA, but all three writes
  // must be performed in this exact order for the transfer to work.
  //
  *BKDMA_CMD2_REG    = *BKDMA_CMD2_REG    | BKDMA_C2_EN;
  *BKDMA_CMD_CFG_REG = *BKDMA_CMD_CFG_REG | BKDMA_CC_CMD_LATCH;
  *BKDMA_CMD_CFG_REG = *BKDMA_CMD_CFG_REG | BKDMA_CC_CMD_START;

  // Wait for IDLE
  do { /* nothing */ } while ((*BKDMA_STATUS_REG & BKDMA_S_IDLE) == 0);

  // Stop & unprepare
  *BKDMA_CMD_CFG_REG = *BKDMA_CMD_CFG_REG & ~BKDMA_CC_CMD_START;
  *BKDMA_CMD_CFG_REG = *BKDMA_CMD_CFG_REG & ~BKDMA_CC_CMD_LATCH;

  // Disable BK DMA peripheral clock
  *SYSTEM_PERIP_CLK_EN1_REG = *SYSTEM_PERIP_CLK_EN1_REG & (~SYSTEM_PERI_BACKUP_CLK_EN);

  // All good!
  return 0;
}
