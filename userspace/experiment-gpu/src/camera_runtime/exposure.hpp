#pragma once

#include "camera_control.hpp"

namespace experiment::camera {

enum class SubjectKind {
    kForeground = 1,
    kPerson = 2,
    kFace = 3
};
struct Subject {
    SubjectKind kind;
    std::uint32_t confidence;
    Rect bounds;
};

inline bool MaskBounds(
    const std::uint8_t *data,
    std::size_t bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t stride,
    std::uint8_t threshold,
    Rect &output
)
{
    const auto required = std::uint64_t(height ? height - 1 : 0) * stride + width;
    if (!data || width == 0 || height == 0 || stride < width || required > bytes || required > SIZE_MAX) {
        return false;
    }
    std::uint32_t left = width, top = height, right = 0, bottom = 0;
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            if (data[std::size_t(row) * stride + column] < threshold) {
                continue;
            }
            if (column < left) {
                left = column;
            }
            if (row < top) {
                top = row;
            }
            right = column > right ? column : right;
            bottom = row > bottom ? row : bottom;
        }
    }
    if (left == width) {
        return false;
    }
    output = {left, top, right - left + 1, bottom - top + 1};
    return true;
}

class ExposureController {
public:
    explicit ExposureController(std::uint32_t grace = 10) : grace_(grace) {}

    bool Propose(
        std::uint32_t sequence,
        const Subject *subjects,
        std::size_t count,
        Rect content,
        Rect sensor_crop,
        Rect &command
    )
    {
        if (have_sequence_ && (sequence == sequence_ || static_cast<std::int32_t>(sequence - sequence_) < 0)) {
            return false;
        }
        have_sequence_ = true;
        sequence_ = sequence;
        const Subject *selected = nullptr;
        Rect proposed;
        if (!MapToSensor(content, content, sensor_crop, proposed) || (!subjects && count != 0)) {
            return false;
        }
        for (std::size_t index = 0; index < count; ++index) {
            const auto &subject = subjects[index];
            if (subject.kind < SubjectKind::kForeground || subject.kind > SubjectKind::kFace || subject.confidence == 0
                || subject.confidence > 1000) {
                continue;
            }
            Rect mapped;
            if (!MapToSensor(subject.bounds, content, sensor_crop, mapped)) {
                continue;
            }
            if (!selected || subject.kind > selected->kind
                || (subject.kind == selected->kind && subject.confidence > selected->confidence)) {
                selected = &subject;
                proposed = mapped;
            }
        }
        if (selected) {
            missing_ = 0;
            if (selected->kind == SubjectKind::kFace) {
                const auto margin = proposed.height / 4;
                const auto top = proposed.y - sensor_crop.y < margin ? sensor_crop.y : proposed.y - margin;
                const auto available = sensor_crop.y + sensor_crop.height - (proposed.y + proposed.height);
                proposed.height += proposed.y - top + (margin < available ? margin : available);
                proposed.y = top;
            }
        } else {
            if (missing_ < grace_) {
                ++missing_;
            }
            if (have_applied_ && missing_ < grace_) {
                return false;
            }
        }
        if (have_applied_ && Similar(proposed, applied_)) {
            return false;
        }
        command = proposed;
        return true;
    }

    void Applied(Rect rectangle)
    {
        applied_ = rectangle;
        have_applied_ = true;
    }

private:
    static bool Near(
        std::uint32_t first,
        std::uint32_t second,
        std::uint32_t tolerance
    )
    {
        return first > second ? first - second <= tolerance : second - first <= tolerance;
    }

    static bool Similar(
        Rect first,
        Rect second
    )
    {
        const auto horizontal = second.width / 10;
        const auto vertical = second.height / 10;
        return Near(first.x, second.x, horizontal) && Near(first.y, second.y, vertical)
            && Near(first.width, second.width, horizontal) && Near(first.height, second.height, vertical);
    }

    std::uint32_t grace_;
    std::uint32_t missing_ = 0;
    std::uint32_t sequence_ = 0;
    bool have_sequence_ = false;
    bool have_applied_ = false;
    Rect applied_{};
};

}