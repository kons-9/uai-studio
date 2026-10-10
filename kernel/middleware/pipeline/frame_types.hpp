#pragma once

#include <cstdint>

#include "middleware/buffer/buffer_types.hpp"
#include "middleware/memory/generated/memory_config.hpp"

namespace uai::ai::pipeline {

/* A completed camera frame transported between the camera, display, and
 * application pipeline. This is intentionally separate from allocator
 * storage types; the sequence belongs to the camera pipeline contract. */
struct CaptureFrame {
    buffer::Buffer buffer{};
    std::uint32_t sequence = 0U;
    std::uint32_t completed_ms = 0U;

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

/* A display handoff token. The underlying buffer is owned by MemoryManager,
 * while this object describes its presentation-stage use. */
struct DisplayBuffer {
    buffer::Buffer buffer{};

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

/* The buffers and lease transported from Pipe2 through the inference lanes. */
struct InferenceFrame {
    buffer::Buffer buffer{};
    buffer::Buffer source{};
    buffer::Buffer scratch{};
    buffer::Buffer outputs[memory_manager::kMemoryConfig.model_output_bytes.size()]{};
    std::uint8_t output_count = 0U;
    std::uint32_t capture_sequence = 0U;
    /* A sequence identifies the camera frame. The lease identifies the
     * ownership instance of the slot and remains unique when a slot is
     * recycled or the 32-bit sequence eventually wraps. */
    std::uint64_t lease_token = 0U;
    bool from_pipe2 = false;
    bool source_valid = false;

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

} // namespace uai::ai::pipeline
