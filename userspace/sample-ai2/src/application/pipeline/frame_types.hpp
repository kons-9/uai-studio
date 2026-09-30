#ifndef UAI_AI_APPLICATION_PIPELINE_FRAME_TYPES_HPP
#define UAI_AI_APPLICATION_PIPELINE_FRAME_TYPES_HPP

#include <cstdint>

#include "middleware/memory/buffer_types.hpp"
#include "memory_manager/memory_config.hpp"

namespace uai::ai::pipeline {

/* A completed camera frame transported between the camera, display, and
 * application pipeline. This is intentionally separate from allocator
 * storage types; the sequence belongs to the camera pipeline contract. */
struct CaptureFrame {
    memory_allocator::Buffer buffer{};
    std::uint32_t sequence = 0U;

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

/* A display handoff token. The underlying buffer is owned by MemoryManager,
 * while this object describes its presentation-stage use. */
struct DisplayBuffer {
    memory_allocator::Buffer buffer{};

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

/* The runtime state transported from Pipe2 through preprocessing, NPU, and
 * postprocessing. It combines allocator buffers with pipeline ownership and
 * model-preparation metadata, so it does not belong to the allocator layer. */
struct InferenceFrame {
    static constexpr std::uint8_t kUnknownModelKindId = 0xFFU;

    memory_allocator::Buffer buffer{};
    memory_allocator::Buffer source{};
    memory_allocator::Buffer scratch{};
    memory_allocator::Buffer
        outputs[memory_manager::kMemoryConfig.model_output_bytes.size()]{};
    std::uint8_t output_count = 0U;
    std::uint32_t capture_sequence = 0U;
    /* A sequence identifies the camera frame. The lease identifies the
     * ownership instance of the slot and remains unique when a slot is
     * recycled or the 32-bit sequence eventually wraps. */
    std::uint64_t lease_token = 0U;
    bool from_pipe2 = false;
    bool source_valid = false;
    bool input_prepared_by_cpu = false;
    bool input_prepared = false;
    std::uint8_t prepared_model_kind_id = kUnknownModelKindId;
    std::uint32_t input_preparation_start_ms = 0U;
    std::uint32_t input_preparation_end_ms = 0U;
    std::uint32_t input_preparation_elapsed_ms = 0U;

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

} // namespace uai::ai::pipeline

#endif
