// Intercept DebugInterrupt and call a handler written in C (Xtensa, windowed ABI)
//
// Interrupt handlers for levels 4, 5, 6 and 7 must be written in assembly.
// This is because the system does not invoke them like a regular C function.
// This file contains an assembly adapter that calls regular C code.
//
// vvb@nym.hush.com
// 

#ifdef __XTENSA__

#include <xtensa/coreasm.h>
#include <xtensa/corebits.h>
#include <xtensa/config/system.h>

#include "sdkconfig.h"
#include "xtensa_rtos.h"
#include "soc/soc.h"
#include "xt_asm_utils.h"
#include "xtensa_context.h"

#define CAUSE_DEBUGEXCEPTION    (1)
#define XT_DEBUGCAUSE_DI        (5)

#if (CONFIG_ESP32_ECO3_CACHE_LOCK_FIX && CONFIG_BTDM_CTRL_HLI)
// ESP32 v3.00 contains a hardware bug. We do not really care about the details,
// and honestly we do not fully understand them either.
//
// The code below contains two workaround snippets copied from xtensa_vectors.S
// (without the slightest understanding of how they actually work).
//
#  define BUGGY_CHIP
#endif


.section    .iram1, "ax"

// User C function
.extern     debug_exception_handler
.type       debug_exception_handler, @function

// Weak symbol, may be overridden
.global     xt_debugexception
.type       xt_debugexception, @function

.align      4
.literal_position
.align      4

// DebugExceptionVector saves register A0 and jumps to xt_debugexception
// A0 is saved using:
// wsr a0, EXCSAVE+XCHAL_DEBUGLEVEL
//
// See xtensa_vectors.S:_DebugExceptionVector
//
xt_debugexception:

#ifdef BUGGY_CHIP

// The original workaround code contains a strange line.
// Commented it out just in case: A0 is already saved,
// no need to trash somebody else's stack.
//
 // s32i a0, sp, XT_STK_EXIT

// Read bit 13 of CPU CoreID.
// If it is 0, we are running on core 0; otherwise on core 1.
  rsr.prid a0
  extui    a0, a0, 13, 1

// Try to determine whether this interrupt came from BT
// or whether this is a regular DebugInterrupt.
//
// If we think BT caused it, execute some magical delay.
// No idea why. Espressif does this in their code,
// so let's not argue.
//
#if (CONFIG_BTDM_CTRL_PINNED_TO_CORE == PRO_CPU_NUM)
  beqz    a0, 1f
#else
  bnez    a0, 1f
#endif

  rsr     a0, DEBUGCAUSE
  extui   a0, a0, XT_DEBUGCAUSE_DI, 1
  bnez    a0, debug_di_exc
1:
#endif // BUGGY_CHIP

// Set exception code in EXCCAUSE just in case.
//
movi    a0, CAUSE_DEBUGEXCEPTION
wsr     a0, EXCCAUSE

// This trampoline code is based on the GDBStub trampoline.
// It should be rewritten eventually: there is no reason to copy
// everything into Level1.
//
// The code should simply use Level6 everywhere.
//
// For now...
//
// Copy EPC and EXCSAVE from DEBUGLEVEL (Level6)
// into EXCEPTIONLEVEL (Level1).
//
rsr     a0,(EPC + XCHAL_DEBUGLEVEL)
wsr     a0,EPC_1

// Restore our A0 saved by _DebugExceptionVector
// and also save it into Level1.
//
rsr     a0,(EXCSAVE + XCHAL_DEBUGLEVEL)
wsr     a0,EXCSAVE_1

// Build trampoline
//
// Interrupt levels 4,5,6 and 7 are high-priority interrupts,
// therefore handlers must be written in assembly.
//
// Since writing EVERYTHING in assembly sounds unpleasant,
// set up an environment allowing us to call a C function.
//
// Allocate stack space for exception frame.
// Pointer to this frame will be passed to the C handler.
//
// The C code may modify the frame contents
// (for example change PC).
//
addi    sp, sp, -XT_STK_FRMSZ

// Start saving context
//
// Save A0 (return address)
s32i    a0, sp, XT_STK_EXIT
s32i    a0, sp, XT_STK_A0

// Save Processor State
rsr     a0, PS
s32i    a0, sp, XT_STK_PS

// Save PC
//
// PC was copied into Level1 above,
// therefore EPC_1 instead of EPC_6
rsr     a0, EPC_1
s32i    a0, sp, XT_STK_PC

// Save standard context
call0   _xt_context_save

// Save SP
addi     a7, sp, XT_STK_FRMSZ
s32i     a7, sp, XT_STK_A1

// _xt_context_save() does not save A12 and A13.
// Have to do it manually :(
s32i    a12, sp, XT_STK_A12
s32i    a13, sp, XT_STK_A13

// Save EXCCAUSE and VADDR.
//
// Not really needed, but keep them for completeness.
rsr     a0, EXCCAUSE
s32i    a0, sp, XT_STK_EXCCAUSE

rsr     a0, EXCVADDR
s32i    a0, sp, XT_STK_EXCVADDR


rsr     a0, EXCSAVE_1      // remove this line later


// Configure PS before calling C:
//
// Clear EXCM and disable level1...level5 interrupts
// (everything except NMI and DEBUGINTERRUPT).
//
movi    a0, PS_INTLEVEL(5) | PS_UM | PS_WOE
wsr     a0, PS

// Save PC.
//
// This value will be available from C as frame->pc.
//
// It may be modified and execution will continue
// at the new PC after handler exit.
//
rsr     a0,(EPC + XCHAL_DEBUGLEVEL)
s32i    a0, sp, XT_STK_PC

// Pass stack pointer to handler.
//
// Stack already contains the exception frame.
//
// After callx4 register A6 becomes A2
// (first function argument).
mov     a6, sp

rsr     a9, EPS_6
s32i    a9, sp, XT_STK_PS

// Finally. Call C handler.
movi    a11, debug_exception_handler
callx4  a11

// Restore EPC_6 from exception frame->pc
//
l32i    a0, sp, XT_STK_PC
wsr     a0,(EPC + XCHAL_DEBUGLEVEL)

// Restore everything and return.
//
// Execution resumes from EPC_6.
call0   _xt_context_restore
l32i    a12, sp, XT_STK_A12
l32i    a13, sp, XT_STK_A13

// Return address (A0)
l32i    a0, sp, XT_STK_EXIT

// Stack (A1==SP)
addi    sp, sp, XT_STK_FRMSZ

rfi     XCHAL_DEBUGLEVEL


#ifdef BUGGY_CHIP

.align 4
debug_di_exc:

  // Just a delay
  movi    a0, 243

2:
  addi    a0, a0, -1
  .rept 4
  nop
  .endr
  bnez    a0, 2b

  // Restore return address (A0) and return
  rsr     a0, EXCSAVE+XCHAL_DEBUGLEVEL
  rfi     XCHAL_DEBUGLEVEL

#endif // BUGGY_CHIP


// Mandatory lines below.
//
// Without them the linker will not pull this file
// into the project.
.global ld_include_xt_debugexception
ld_include_xt_debugexception:

#endif // __XTENSA__
