/*
 * ota_loader.h — OTA function loader (receives probe binaries via MCP)
 *
 * Manages the lifecycle of dynamically-deployed probe functions:
 *   1. Receive binary data (Base64 or raw) from PC via MCP
 *   2. Validate (size check, entry point alignment)
 *   3. Place into ProbeArena (RAM only, no Flash writes)
 *   4. Call on demand, measure execution time
 *   5. Report results via MCP resource
 *
 * Safety:
 *   - Probes run in RAM arena only (no Flash modification)
 *   - Size limit per probe (ProbeArena budget)
 *   - Entry point must be word-aligned
 *   - MPU can restrict arena to execute-only (platform config)
 */
#pragma once

#include "probe_arena.h"

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace uai {

/* ------------------------------------------------------------------ */
/*  Base64 decoder (for receiving binary via JSON)                    */
/* ------------------------------------------------------------------ */

namespace detail {

inline int b64_char_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1; /* padding or invalid */
}

/* Decode base64 in-place. Returns decoded length. */
inline size_t b64_decode(const char *src, size_t src_len,
                         uint8_t *dst, size_t dst_cap)
{
    size_t si = 0, di = 0;
    uint32_t acc = 0;
    int bits = 0;

    while (si < src_len && di < dst_cap) {
        int val = b64_char_val(src[si++]);
        if (val < 0) continue; /* skip padding, whitespace */
        acc = (acc << 6) | static_cast<uint32_t>(val);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            dst[di++] = static_cast<uint8_t>((acc >> bits) & 0xFF);
        }
    }
    return di;
}

} // namespace detail

/* ------------------------------------------------------------------ */
/*  OTA Loader                                                         */
/* ------------------------------------------------------------------ */

template <size_t ArenaSize = 4096>
class OtaLoader {
public:
    OtaLoader()
    {
        arena_.set_timestamp_fn(timestamp_fn_);
    }

    /* Deploy a probe from raw binary data */
    int deploy_raw(const char *name, const uint8_t *binary, uint32_t size)
    {
        if (!validate(binary, size)) return -10;
        return arena_.load(name, binary, size);
    }

    /* Deploy a probe from Base64-encoded data */
    int deploy_b64(const char *name, const char *b64, size_t b64_len)
    {
        /* Decode into temporary buffer (on stack, limited size) */
        uint8_t decoded[1024];
        size_t decoded_len = detail::b64_decode(b64, b64_len,
                                                 decoded, sizeof(decoded));
        if (decoded_len == 0) return -11;

        return deploy_raw(name, decoded, static_cast<uint32_t>(decoded_len));
    }

    /* Execute a deployed probe by slot index */
    int execute(int slot_idx, uint32_t &elapsed_us)
    {
        return arena_.call(slot_idx, elapsed_us);
    }

    /* Execute a deployed probe by name */
    int execute_by_name(const char *name, uint32_t &elapsed_us)
    {
        int idx = find_by_name(name);
        if (idx < 0) return -1;
        return arena_.call(idx, elapsed_us);
    }

    /* Remove a probe by slot index */
    int remove(int slot_idx)
    {
        return arena_.unload(slot_idx);
    }

    /* Remove a probe by name */
    int remove_by_name(const char *name)
    {
        int idx = find_by_name(name);
        if (idx < 0) return -1;
        return arena_.unload(idx);
    }

    /* Find probe slot by name. Returns index or -1. */
    int find_by_name(const char *name) const
    {
        for (size_t i = 0; i < MAX_PROBES; i++) {
            const auto *slot = arena_.get_slot(static_cast<int>(i));
            if (slot && std::strcmp(slot->name, name) == 0) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /* Get measurement results for a probe */
    const ProbeSlot *get_results(int slot_idx) const
    {
        return arena_.get_slot(slot_idx);
    }

    /* Arena access */
    ProbeArena<ArenaSize>       &arena()       { return arena_; }
    const ProbeArena<ArenaSize> &arena() const { return arena_; }

    /* Set timestamp function */
    using TimestampFn = uint32_t (*)();
    void set_timestamp_fn(TimestampFn fn)
    {
        timestamp_fn_ = fn;
        arena_.set_timestamp_fn(fn);
    }

private:
    /* Basic binary validation */
    bool validate(const uint8_t *binary, uint32_t size) const
    {
        if (!binary || size < 4) return false;
        if (size > ArenaSize) return false;

        /* Check alignment (Thumb code: size should be even) */
        if (size & 1) return false;

        return true;
    }

    ProbeArena<ArenaSize> arena_;
    TimestampFn           timestamp_fn_ = default_ts;

    static uint32_t default_ts() { return 0; }
};

/* Default OTA loader type (4 KB arena) */
using DefaultOtaLoader = OtaLoader<4096>;

} // namespace uai
