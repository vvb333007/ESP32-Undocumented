//
// This is an example for ESP32, ESP32-S2 and ESP32-S3 boards: 
// detect and catch CPU exceptions, 
// skip failed CPU instruction and continue execution as if nothing happened
//
// For the simplicity sake the code below is written for Arduino Framework
// 
// vvb@nym.hush.com, vvb333007@gmail.com
//
#include <Arduino.h>
#include <stdint.h>

// Xtensa instruction length, in bytes, by the instruction's first octet
//
static const uint8_t xt_insn_len[] = { XCHAL_BYTE0_FORMAT_LENGTHS };


// Exception number we wanna catch (#6 -> "Division By Zero").
// -----------------------------------------------------------
// There are 64 exceptions in total, but we're just gonna mess with divide-by-zero here.
// You can change this number to any other exception number (Access to NULL pointer,
// IllegalInstruction, UnalignedMemoryAccess, calling a function by wrong pointer etc)
//
#define EXCEPTION_NUMBER 6


// This struct gets passed to our exception handler.
// You can poke at it to change the processor state.
//
struct exc_frame {
  uint32_t unknown0;         // Have no idea
  uint32_t pc;               // Pointer to the current instruction (Program Counter)
  uint32_t known[20];        // Bunch of registers a0–a15 plus some special ones
};


// Table of exception handler pointers - one per core, 64 entries each.
// Set up when the system boots.
//
// Handlers get called by the core whenever something nasty happens.
// Most exceptions hit the same function, which eventually calls panic(),
// shows "Guru Meditation", dumps registers, and your program freezes.
//
// Some exceptions are handled quietly - like unaligned memory access (DRAM only).
// The core fixes the access and keeps going.
//

typedef void (*funcptr_t)(struct exc_frame *);

// This one is defined by the linker
extern funcptr_t _xt_exception_table[];


// Our custom exception handler: just skip the offending instruction.
// -----------------------------------------------------------------
// Instruction length is determined from the first byte.
// Xtensa instructions can be weird (3 bytes, anyone?), so PC might be unaligned.
// To grab that first byte from the IBUS , we do a little trick:
// read the aligned 32-bit word and then extract the byte we need; it must be aligned 32bit access only :-/
//
// (NB: If you are intercepring division by zero, then following function 
// can be reduced down to "c->pc += 3"). Below is the generic handler for all exception types
//
// The function gets a pointer to the Exception stack frame - all the registers at the
// time of exception and the instruction pointer (pc) that caused it.
//
// We just tweak PC to point to the next instruction and return.
// Division result? Who knows. But the program keeps running.
//
// Don't try calling delay() or anything scheduler-related here.
// Printing is okay, but do it carefully via extern ets_printf(const char *, ...)
//
static void exception_handler(struct exc_frame *f) {

  uintptr_t pc = (uintptr_t)f->pc;

  // Align the address to 4 bytes
  uint32_t aligned = pc & ~3;  

  // Read 32 bits from the aligned address
  uint32_t word = *(volatile uint32_t*)aligned;

  // Shift to get the byte we actually want
  uint32_t shift = (pc & 3) * 8;

  // Grab that first byte of the instruction
  uint8_t opcode = (word >> shift) & 255;

  // Move PC to the next instruction
  f->pc += xt_insn_len[opcode];
}


// Divider. We will set it to zero a bit later
volatile int j = 1;

void setup() {

  Serial.begin(115200);

  // Hook up our exception handler in the table.
  // Do it for both cores if we have more than 1 core.
  // Comment these lines out and the sketch will hang, like it normally would.
  //
  // Core#0
  //
  _xt_exception_table[EXCEPTION_NUMBER * portNUM_PROCESSORS + 0] = exception_handler;  
#if portNUM_PROCESSORS > 1
  // Core#1
  //
  _xt_exception_table[EXCEPTION_NUMBER * portNUM_PROCESSORS + 1] = exception_handler;  
#endif

  // Set the bomb
  j = 0;
}


// Moment of truth. Inside the loop.
//
void loop() {

  delay(1000);

  // Check if j == 0
  Serial.printf("Hello! :) \"j\" is %d\r\n", j);

  // Will it crash or not? Let's find out.
  for (int i = 0x61; i < 0x6e; i++)
    Serial.printf("%% %x\r\n", i / j);

}

