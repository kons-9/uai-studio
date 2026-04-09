/*
 * bytecode.h — Bytecode instruction set for the Mini-VM filter engine
 *
 * Design principles (safety-first for MCU):
 *   - No loops (no backward jumps)
 *   - Instruction count limit (MAX_INSNS = 64)
 *   - Read-only memory access (event fields only)
 *   - Stack depth limit (MAX_STACK = 16)
 *   - Deterministic execution time
 *
 * Stack-based architecture. The VM evaluates a filter program
 * against a TraceEvent and returns bool (keep / drop).
 */
#pragma once

#include <cstdint>

namespace uai {

/* ------------------------------------------------------------------ */
/*  Opcodes (1 byte each)                                              */
/* ------------------------------------------------------------------ */

enum class Opcode : uint8_t {
    /* Stack manipulation */
    NOP        = 0x00,
    PUSH_IMM8  = 0x01,  /* push 8-bit immediate */
    PUSH_IMM16 = 0x02,  /* push 16-bit immediate (LE) */
    PUSH_IMM32 = 0x03,  /* push 32-bit immediate (LE) */
    DUP        = 0x04,  /* duplicate top of stack */
    POP        = 0x05,  /* discard top */

    /* Event field access (read-only) */
    LOAD_TIMESTAMP  = 0x10,  /* push event.timestamp_us */
    LOAD_TASK_ID    = 0x11,  /* push event.task_id */
    LOAD_EVENT_TYPE = 0x12,  /* push event.type */
    LOAD_FLAGS      = 0x13,  /* push event.flags */
    LOAD_SEQ        = 0x14,  /* push event.seq */
    LOAD_PAYLOAD0   = 0x15,  /* push first 4 bytes of payload as u32 */
    LOAD_PAYLOAD4   = 0x16,  /* push next 4 bytes of payload as u32 */

    /* Arithmetic (unsigned 32-bit) */
    ADD = 0x20,  /* a + b */
    SUB = 0x21,  /* a - b */
    MUL = 0x22,  /* a * b */
    DIV = 0x23,  /* a / b (div-by-zero → 0) */
    MOD = 0x24,  /* a % b (div-by-zero → 0) */

    /* Bitwise */
    AND = 0x30,
    OR  = 0x31,
    XOR = 0x32,
    NOT = 0x33,  /* bitwise NOT (unary) */
    SHL = 0x34,
    SHR = 0x35,

    /* Comparison → pushes 1 (true) or 0 (false) */
    EQ  = 0x40,  /* a == b */
    NE  = 0x41,  /* a != b */
    LT  = 0x42,  /* a <  b */
    LE  = 0x43,  /* a <= b */
    GT  = 0x44,  /* a >  b */
    GE  = 0x45,  /* a >= b */

    /* Logical */
    LOGIC_AND = 0x50,  /* (a != 0) && (b != 0) → 0 or 1 */
    LOGIC_OR  = 0x51,  /* (a != 0) || (b != 0) → 0 or 1 */
    LOGIC_NOT = 0x52,  /* !(a != 0) → 0 or 1 */

    /* Control */
    RET = 0xFF,  /* return TOS as filter result (0=drop, nonzero=keep) */
};

/* ------------------------------------------------------------------ */
/*  Program container                                                  */
/* ------------------------------------------------------------------ */

inline constexpr size_t VM_MAX_PROGRAM = 256;  /* max bytecode bytes */

struct VmProgram {
    uint8_t  code[VM_MAX_PROGRAM];
    uint16_t length;  /* actual bytecode length */
};

/* ------------------------------------------------------------------ */
/*  VM limits                                                          */
/* ------------------------------------------------------------------ */

inline constexpr size_t VM_MAX_INSNS = 64;   /* instruction count limit */
inline constexpr size_t VM_MAX_STACK = 16;   /* stack depth limit       */

/* ------------------------------------------------------------------ */
/*  VM error codes                                                     */
/* ------------------------------------------------------------------ */

enum class VmError : int8_t {
    OK              =  0,
    STACK_OVERFLOW  = -1,
    STACK_UNDERFLOW = -2,
    INSN_LIMIT      = -3,
    INVALID_OPCODE  = -4,
    PROGRAM_TOO_BIG = -5,
    NO_RETURN       = -6,
};

} // namespace uai
