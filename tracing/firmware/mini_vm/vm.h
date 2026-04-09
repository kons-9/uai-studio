/*
 * vm.h — Mini-VM: safe bytecode execution engine for trace filters
 *
 * Executes a VmProgram against a TraceEvent. Returns:
 *   true  → keep the event (pass through filter)
 *   false → drop the event
 *
 * Safety guarantees:
 *   - No loops (no backward jumps; linear execution only)
 *   - Instruction count bounded by VM_MAX_INSNS
 *   - Stack depth bounded by VM_MAX_STACK
 *   - Read-only access to event fields only
 *   - Deterministic worst-case execution time
 */
#pragma once

#include "bytecode.h"
#include "../tracepoint.h"

#include <cstring>

namespace uai {

class MiniVm {
public:
    struct Result {
        bool    pass;   /* true = keep event */
        VmError error;  /* OK if no error */
    };

    /* Execute program against event. */
    Result execute(const VmProgram &prog, const TraceEvent &event)
    {
        if (prog.length > VM_MAX_PROGRAM) {
            return { false, VmError::PROGRAM_TOO_BIG };
        }

        uint32_t stack[VM_MAX_STACK];
        size_t   sp = 0;  /* stack pointer (points to next free slot) */
        size_t   pc = 0;  /* program counter */
        size_t   insn_count = 0;

        auto push = [&](uint32_t val) -> VmError {
            if (sp >= VM_MAX_STACK) return VmError::STACK_OVERFLOW;
            stack[sp++] = val;
            return VmError::OK;
        };

        auto pop = [&](uint32_t &val) -> VmError {
            if (sp == 0) return VmError::STACK_UNDERFLOW;
            val = stack[--sp];
            return VmError::OK;
        };

        auto pop2 = [&](uint32_t &a, uint32_t &b) -> VmError {
            if (sp < 2) return VmError::STACK_UNDERFLOW;
            b = stack[--sp];
            a = stack[--sp];
            return VmError::OK;
        };

        while (pc < prog.length) {
            if (++insn_count > VM_MAX_INSNS) {
                return { false, VmError::INSN_LIMIT };
            }

            auto op = static_cast<Opcode>(prog.code[pc++]);
            VmError err;
            uint32_t a, b, val;

            switch (op) {

            /* ---- Stack manipulation ---- */
            case Opcode::NOP:
                break;

            case Opcode::PUSH_IMM8:
                if (pc + 1 > prog.length) return { false, VmError::INVALID_OPCODE };
                err = push(prog.code[pc++]);
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::PUSH_IMM16:
                if (pc + 2 > prog.length) return { false, VmError::INVALID_OPCODE };
                val = static_cast<uint32_t>(prog.code[pc])
                    | (static_cast<uint32_t>(prog.code[pc + 1]) << 8);
                pc += 2;
                err = push(val);
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::PUSH_IMM32:
                if (pc + 4 > prog.length) return { false, VmError::INVALID_OPCODE };
                std::memcpy(&val, &prog.code[pc], 4);
                pc += 4;
                err = push(val);
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::DUP:
                if (sp == 0) return { false, VmError::STACK_UNDERFLOW };
                err = push(stack[sp - 1]);
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::POP:
                err = pop(val);
                if (err != VmError::OK) return { false, err };
                break;

            /* ---- Event field loads ---- */
            case Opcode::LOAD_TIMESTAMP:
                err = push(event.timestamp_us);
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::LOAD_TASK_ID:
                err = push(event.task_id);
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::LOAD_EVENT_TYPE:
                err = push(static_cast<uint32_t>(event.type));
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::LOAD_FLAGS:
                err = push(event.flags);
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::LOAD_SEQ:
                err = push(event.seq);
                if (err != VmError::OK) return { false, err };
                break;

            case Opcode::LOAD_PAYLOAD0: {
                uint32_t p0;
                std::memcpy(&p0, &event.payload.raw[0], 4);
                err = push(p0);
                if (err != VmError::OK) return { false, err };
                break;
            }

            case Opcode::LOAD_PAYLOAD4: {
                uint32_t p4;
                std::memcpy(&p4, &event.payload.raw[4], 4);
                err = push(p4);
                if (err != VmError::OK) return { false, err };
                break;
            }

            /* ---- Arithmetic ---- */
            case Opcode::ADD:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a + b); if (err != VmError::OK) return { false, err };
                break;
            case Opcode::SUB:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a - b); if (err != VmError::OK) return { false, err };
                break;
            case Opcode::MUL:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a * b); if (err != VmError::OK) return { false, err };
                break;
            case Opcode::DIV:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(b != 0 ? a / b : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::MOD:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(b != 0 ? a % b : 0);
                if (err != VmError::OK) return { false, err };
                break;

            /* ---- Bitwise ---- */
            case Opcode::AND:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a & b); if (err != VmError::OK) return { false, err };
                break;
            case Opcode::OR:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a | b); if (err != VmError::OK) return { false, err };
                break;
            case Opcode::XOR:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a ^ b); if (err != VmError::OK) return { false, err };
                break;
            case Opcode::NOT:
                err = pop(a); if (err != VmError::OK) return { false, err };
                err = push(~a); if (err != VmError::OK) return { false, err };
                break;
            case Opcode::SHL:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(b < 32 ? a << b : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::SHR:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(b < 32 ? a >> b : 0);
                if (err != VmError::OK) return { false, err };
                break;

            /* ---- Comparison ---- */
            case Opcode::EQ:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a == b ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::NE:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a != b ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::LT:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a < b ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::LE:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a <= b ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::GT:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a > b ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::GE:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push(a >= b ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;

            /* ---- Logical ---- */
            case Opcode::LOGIC_AND:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push((a != 0 && b != 0) ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::LOGIC_OR:
                err = pop2(a, b); if (err != VmError::OK) return { false, err };
                err = push((a != 0 || b != 0) ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;
            case Opcode::LOGIC_NOT:
                err = pop(a); if (err != VmError::OK) return { false, err };
                err = push(a == 0 ? 1 : 0);
                if (err != VmError::OK) return { false, err };
                break;

            /* ---- Control ---- */
            case Opcode::RET:
                if (sp == 0) return { false, VmError::STACK_UNDERFLOW };
                return { stack[sp - 1] != 0, VmError::OK };

            default:
                return { false, VmError::INVALID_OPCODE };
            }
        }

        /* Fell off end without RET */
        return { false, VmError::NO_RETURN };
    }
};

/* ------------------------------------------------------------------ */
/*  Helper: build simple filter programs                               */
/* ------------------------------------------------------------------ */

/* Filter: keep only events from a specific task */
inline VmProgram vm_filter_task_id(uint16_t task_id)
{
    VmProgram prog{};
    size_t pc = 0;
    prog.code[pc++] = static_cast<uint8_t>(Opcode::LOAD_TASK_ID);
    prog.code[pc++] = static_cast<uint8_t>(Opcode::PUSH_IMM16);
    prog.code[pc++] = static_cast<uint8_t>(task_id & 0xFF);
    prog.code[pc++] = static_cast<uint8_t>((task_id >> 8) & 0xFF);
    prog.code[pc++] = static_cast<uint8_t>(Opcode::EQ);
    prog.code[pc++] = static_cast<uint8_t>(Opcode::RET);
    prog.length = static_cast<uint16_t>(pc);
    return prog;
}

/* Filter: keep only events of a specific type */
inline VmProgram vm_filter_event_type(TraceEventType type)
{
    VmProgram prog{};
    size_t pc = 0;
    prog.code[pc++] = static_cast<uint8_t>(Opcode::LOAD_EVENT_TYPE);
    prog.code[pc++] = static_cast<uint8_t>(Opcode::PUSH_IMM8);
    prog.code[pc++] = static_cast<uint8_t>(type);
    prog.code[pc++] = static_cast<uint8_t>(Opcode::EQ);
    prog.code[pc++] = static_cast<uint8_t>(Opcode::RET);
    prog.length = static_cast<uint16_t>(pc);
    return prog;
}

} // namespace uai
