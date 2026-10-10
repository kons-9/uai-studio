#pragma once

#include <cstddef>
#include <cstdint>

#include "middleware/ai_runtime/inference_result_types.hpp"
#include "task/model_result_snapshot.hpp"

namespace uai::ai::task {

template <std::size_t ModelCount, std::size_t ScheduleLength>
struct PipelineSubmitState {
    static_assert(ModelCount > 0U && ScheduleLength > 0U);

    std::uint32_t submitted_count[ModelCount]{};
    std::size_t next_schedule_index = 0U;

    std::size_t CandidateIndex(std::size_t offset) const { return (next_schedule_index + offset) % ScheduleLength; }

    void CommitSubmission(
        std::size_t model_index,
        std::size_t schedule_index
    )
    {
        ++submitted_count[model_index];
        next_schedule_index = (schedule_index + 1U) % ScheduleLength;
    }
};

struct PostprocessState {
    inference::BoxSet latest_boxes{};
    std::uint32_t completed_count = 0U;

    void Reset(std::uint32_t generation)
    {
        latest_boxes = {};
        stamps_ = {};
        generation_ = generation;
    }

    ModelResultSnapshot Snapshot() const { return {latest_boxes, stamps_, generation_}; }
    std::uint32_t Generation() const { return generation_; }

    const inference::BoxSet &Merge(
        const inference::BoxSet &source,
        std::uint32_t completed_ms = 0U
    )
    {
        const std::uint32_t sequence = latest_boxes.model_sequence + 1U;
        if (source.person_valid) {
            latest_boxes.person = source.person;
            latest_boxes.person_valid = true;
            stamps_[0U] = {true, completed_ms, sequence};
        }
        if (source.face_valid) {
            latest_boxes.face = source.face;
            latest_boxes.face_valid = true;
            stamps_[1U] = {true, completed_ms, sequence};
        }
        if (source.segmentation_valid) {
            latest_boxes.segmentation = source.segmentation;
            latest_boxes.segmentation_valid = true;
            stamps_[2U] = {true, completed_ms, sequence};
        }
        ++latest_boxes.model_sequence;
        latest_boxes.capture_sequence = source.capture_sequence;
        ++completed_count;
        return latest_boxes;
    }

private:
    std::array<ModelResultStamp, 3U> stamps_{};
    std::uint32_t generation_ = 0U;
};

}