#pragma once

#include <cstddef>
#include <cstdint>

#include "shell/engine.hpp"

namespace uai::ai::shell {

inline std::uint32_t TraceCrc(const std::uint8_t *bytes, std::size_t size)
{
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
    }
    return ~crc;
}

inline void StreamTrace(const Output &out, const char *format, unsigned version,
    const std::uint8_t *bytes, std::size_t size, void (*yield)() = nullptr)
{
    constexpr char hex[] = "0123456789abcdef";
    const std::uint32_t crc = TraceCrc(bytes, size);
    out.Printf("@TRACE BEGIN format=%s version=%u length=%lu crc=%08lx\r\n",
        format, version, static_cast<unsigned long>(size), static_cast<unsigned long>(crc));
    for (std::size_t offset = 0; offset < size; offset += 32U) {
        char line[96]{};
        const int prefix = std::snprintf(line, sizeof(line), "@TRACE %08lx ", static_cast<unsigned long>(offset));
        std::size_t used = static_cast<std::size_t>(prefix);
        const std::size_t count = size - offset < 32U ? size - offset : 32U;
        for (std::size_t index = 0; index < count; ++index) {
            line[used++] = hex[bytes[offset + index] >> 4U];
            line[used++] = hex[bytes[offset + index] & 0x0fU];
        }
        line[used++] = '\r';
        line[used++] = '\n';
        line[used] = '\0';
        out.Write(line);
        if (yield != nullptr && (offset / 32U) % 16U == 15U)
            yield();
    }
    out.Printf("@TRACE END crc=%08lx\r\n", static_cast<unsigned long>(crc));
}

} // namespace uai::ai::shell