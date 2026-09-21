/*
 * probe_arena.h — Memory arena for OTA-deployed probe functions
 *
 * A dedicated RAM region where dynamically-loaded probe binaries
 * are placed. On STM32 with MPU, this region can be configured
 * with execute-only + no-write-to-kernel permissions for safety.
 *
 * The arena is a simple bump allocator with per-probe slots.
 *
 * Template parameter ArenaSize controls total arena RAM budget.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace uai {

/* ------------------------------------------------------------------ */
/*  Probe slot metadata                                                */
/* ------------------------------------------------------------------ */

struct ProbeSlot {
    char     name[16];       /* probe identifier                      */
    uint8_t *code;           /* pointer into arena memory             */
    uint32_t code_size;      /* binary size in bytes                  */
    bool     active;         /* slot in use                           */

    /* Measurement results */
    uint32_t call_count;     /* number of calls                       */
    uint32_t total_us;       /* total execution time (μs)             */
    uint32_t min_us;         /* minimum execution time (μs)           */
    uint32_t max_us;         /* maximum execution time (μs)           */
};

/* ------------------------------------------------------------------ */
/*  Probe arena                                                        */
/* ------------------------------------------------------------------ */

inline constexpr size_t MAX_PROBES = 8;

template <size_t ArenaSize = 4096>
class ProbeArena {
public:
    ProbeArena() { clear(); }

    /* Allocate space and load a probe binary.
     * Returns slot index (>=0) or negative error. */
    int load(const char *name, const uint8_t *binary, uint32_t size)
    {
        if (!name || !binary || size == 0) return -1;
        if (size > ArenaSize) return -2;  /* too large */

        /* Check available space */
        if (used_ + size > ArenaSize) return -3;  /* arena full */

        /* Find a free slot */
        int idx = -1;
        for (size_t i = 0; i < MAX_PROBES; i++) {
            if (!slots_[i].active) { idx = static_cast<int>(i); break; }
        }
        if (idx < 0) return -4;  /* no free slots */

        /* Simple validation: size must be reasonable */
        if (size < 4) return -5;  /* too small to be valid code */

        /* Copy binary into arena */
        auto &slot = slots_[idx];
        slot.code = &arena_[used_];
        std::memcpy(slot.code, binary, size);
        slot.code_size = size;
        slot.active = true;
        slot.call_count = 0;
        slot.total_us = 0;
        slot.min_us = UINT32_MAX;
        slot.max_us = 0;
        std::memset(slot.name, 0, sizeof(slot.name));
        std::strncpy(slot.name, name, sizeof(slot.name) - 1);

        used_ += size;
        /* Align to 4 bytes */
        used_ = (used_ + 3) & ~3u;

        return idx;
    }

    /* Remove a probe by slot index */
    int unload(int idx)
    {
        if (idx < 0 || static_cast<size_t>(idx) >= MAX_PROBES) return -1;
        if (!slots_[idx].active) return -2;

        slots_[idx].active = false;
        /* Note: arena space is not reclaimed (bump allocator).
         * Call compact() if needed. */
        return 0;
    }

    /* Execute a probe by slot index.
     * The probe is called as: void (*)(void)
     * Returns execution time in μs. */
    int call(int idx, uint32_t &elapsed_us)
    {
        if (idx < 0 || static_cast<size_t>(idx) >= MAX_PROBES) return -1;
        if (!slots_[idx].active) return -2;

        auto &slot = slots_[idx];

        /* Cast arena memory to function pointer (Thumb mode: set bit 0) */
        using ProbeFn = void (*)();
        auto fn = reinterpret_cast<ProbeFn>(
            reinterpret_cast<uintptr_t>(slot.code) | 1u);

        uint32_t start = timestamp_us_();
        fn();
        uint32_t end = timestamp_us_();

        elapsed_us = end - start;

        /* Update statistics */
        slot.call_count++;
        slot.total_us += elapsed_us;
        if (elapsed_us < slot.min_us) slot.min_us = elapsed_us;
        if (elapsed_us > slot.max_us) slot.max_us = elapsed_us;

        return 0;
    }

    /* Get probe slot info */
    const ProbeSlot *get_slot(int idx) const
    {
        if (idx < 0 || static_cast<size_t>(idx) >= MAX_PROBES) return nullptr;
        return slots_[idx].active ? &slots_[idx] : nullptr;
    }

    /* Iterate active probes */
    size_t active_count() const
    {
        size_t count = 0;
        for (size_t i = 0; i < MAX_PROBES; i++) {
            if (slots_[i].active) count++;
        }
        return count;
    }

    size_t arena_used() const  { return used_; }
    size_t arena_total() const { return ArenaSize; }

    /* Reset arena (remove all probes) */
    void clear()
    {
        used_ = 0;
        for (auto &s : slots_) {
            s.active = false;
            s.call_count = 0;
            s.total_us = 0;
            s.min_us = UINT32_MAX;
            s.max_us = 0;
        }
        std::memset(arena_, 0, ArenaSize);
    }

    /* Set timestamp function (platform-specific) */
    using TimestampFn = uint32_t (*)();
    void set_timestamp_fn(TimestampFn fn) { timestamp_us_ = fn; }

private:
    uint8_t   arena_[ArenaSize] __attribute__((aligned(4))){};
    size_t    used_ = 0;
    ProbeSlot slots_[MAX_PROBES]{};

    TimestampFn timestamp_us_ = default_timestamp;

    static uint32_t default_timestamp() { return 0; }
};

} // namespace uai
