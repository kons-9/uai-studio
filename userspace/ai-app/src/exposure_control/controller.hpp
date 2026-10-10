#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdint>

#include "middleware/ai_runtime/inference_result_types.hpp"

namespace uai::ai::exposure_control {

struct Rect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct Mapping {
    std::uint32_t sensor_width = 0;
    std::uint32_t sensor_height = 0;
    Rect crop{};
    Rect content{0, 96, 480, 288};
    std::uint32_t input_width = 480;
    std::uint32_t input_height = 480;
    bool horizontal = false;
    bool vertical = false;
    Rect detection_content{0, 0, 800, 480};
};

enum class Source { kFullFrame, kFace, kPerson, kForeground };

struct Values {
    Source source = Source::kFullFrame;
    Rect requested{};
    Rect applied{};
    std::int32_t exposure_us = 0;
    std::int32_t gain_mdB = 0;
    bool auto_exposure = false;
    bool available = false;
    std::uint32_t error_code = 0;
};

class Controller final {
public:
    static constexpr std::uint32_t kHoldMs = 1000;

    void Observe(const inference::BoxSet &result, std::uint32_t now)
    {
        if (result.face_valid && Accept(result.capture_sequence, face_sequence_, have_face_)) {
            if (result.face.count != 0) {
                latest_.face = result.face;
                face_tick_ = now;
            }
            have_face_ = true;
        }
        if (result.person_valid && Accept(result.capture_sequence, person_sequence_, have_person_)) {
            if (result.person.count != 0) {
                latest_.person = result.person;
                person_tick_ = now;
            }
            have_person_ = true;
        }
        if (result.segmentation_valid && Accept(result.capture_sequence, foreground_sequence_, have_foreground_)) {
            if (result.segmentation.mask_foreground_pixels >= 4) {
                latest_.segmentation = result.segmentation;
                foreground_tick_ = now;
            }
            have_foreground_ = true;
        }
    }

    bool Step(std::uint32_t now, const Mapping &mapping)
    {
        Rect selected{};
        Source source = Source::kFullFrame;
        if (have_face_ && now - face_tick_ < kHoldMs)
            selected = Select(latest_.face, mapping);
        if (selected.width != 0) {
            source = Source::kFace;
        } else {
            if (have_person_ && now - person_tick_ < kHoldMs)
                selected = Select(latest_.person, mapping);
            if (selected.width != 0) {
                source = Source::kPerson;
            } else if (have_foreground_ && now - foreground_tick_ < kHoldMs) {
                selected = Foreground(latest_.segmentation, mapping);
                if (selected.width != 0)
                    source = Source::kForeground;
            }
        }
        if (selected.width == 0)
            selected = {0, 0, mapping.sensor_width, mapping.sensor_height};
        if (selected.width == 0 || selected.height == 0)
            return false;
        if (source == values_.source && Similar(selected, values_.requested))
            return false;
        values_.source = source;
        values_.requested = selected;
        return true;
    }

    const Values &DisplayValues() const { return values_; }
    void Readback(Rect applied, std::int32_t exposure_us, std::int32_t gain_mdB, bool automatic)
    {
        values_.applied = applied;
        values_.exposure_us = exposure_us;
        values_.gain_mdB = gain_mdB;
        values_.auto_exposure = automatic;
        values_.available = true;
        values_.error_code = 0;
    }
    void Failed(std::uint32_t code) { values_.error_code = code; }
    static void Format(const Values &values, char *text, std::size_t capacity)
    {
        if (values.error_code != 0) {
            std::snprintf(text, capacity, "AE ERROR %u", static_cast<unsigned int>(values.error_code));
        } else if (!values.available) {
            std::snprintf(text, capacity, "AE WAITING");
        } else {
            const char *source = values.source == Source::kFace ? "FACE" : values.source == Source::kPerson ? "PERSON"
                : values.source == Source::kForeground ? "MASK" : "FULL";
            std::snprintf(text, capacity, "%s %s %ldus %ldmdB", values.auto_exposure ? "AE" : "MANUAL", source,
                static_cast<long>(values.exposure_us), static_cast<long>(values.gain_mdB));
        }
    }

private:
    static bool Accept(std::uint32_t sequence, std::uint32_t &previous, bool have)
    {
        if (have && static_cast<std::int32_t>(sequence - previous) <= 0)
            return false;
        previous = sequence;
        return true;
    }

    static bool Similar(const Rect &next, const Rect &previous)
    {
        if (previous.width == 0 || previous.height == 0)
            return false;
        const auto close = [](std::uint32_t next_value, std::uint32_t previous_value, std::uint32_t size) {
            const auto difference = next_value > previous_value ? next_value - previous_value : previous_value - next_value;
            return difference <= std::max<std::uint32_t>(1U, size / 10U);
        };
        return close(next.x, previous.x, previous.width) && close(next.y, previous.y, previous.height)
            && close(next.width, previous.width, previous.width) && close(next.height, previous.height, previous.height);
    }

    static Rect Map(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, const Mapping &mapping)
    {
        const auto &content = mapping.content;
        const auto &crop = mapping.crop;
        if (content.width == 0 || content.height == 0 || crop.width == 0 || crop.height == 0
            || crop.x >= mapping.sensor_width || crop.y >= mapping.sensor_height
            || crop.width > mapping.sensor_width - crop.x || crop.height > mapping.sensor_height - crop.y)
            return {};
        left = std::max(left, static_cast<std::int32_t>(content.x));
        top = std::max(top, static_cast<std::int32_t>(content.y));
        right = std::min(right, static_cast<std::int32_t>(content.x + content.width));
        bottom = std::min(bottom, static_cast<std::int32_t>(content.y + content.height));
        if (right <= left || bottom <= top)
            return {};
        auto horizontal_begin = static_cast<std::uint32_t>(left) - content.x;
        auto horizontal_end = static_cast<std::uint32_t>(right) - content.x;
        auto vertical_begin = static_cast<std::uint32_t>(top) - content.y;
        auto vertical_end = static_cast<std::uint32_t>(bottom) - content.y;
        if (mapping.horizontal) {
            const auto previous = horizontal_begin;
            horizontal_begin = content.width - horizontal_end;
            horizontal_end = content.width - previous;
        }
        if (mapping.vertical) {
            const auto previous = vertical_begin;
            vertical_begin = content.height - vertical_end;
            vertical_end = content.height - previous;
        }
        const auto scale = [](std::uint32_t value, std::uint32_t extent, std::uint32_t size, bool ceiling) {
            return static_cast<std::uint32_t>((static_cast<std::uint64_t>(value) * extent + (ceiling ? size - 1 : 0)) / size);
        };
        const auto sensor_left = scale(horizontal_begin, crop.width, content.width, false);
        const auto sensor_right = scale(horizontal_end, crop.width, content.width, true);
        const auto sensor_top = scale(vertical_begin, crop.height, content.height, false);
        const auto sensor_bottom = scale(vertical_end, crop.height, content.height, true);
        return {crop.x + sensor_left, crop.y + sensor_top, sensor_right - sensor_left, sensor_bottom - sensor_top};
    }

    static Rect Select(const inference::DetectionSet &detections, const Mapping &mapping)
    {
        auto detection_mapping = mapping;
        detection_mapping.content = mapping.detection_content;
        Rect selected{};
        float confidence = 0.0F;
        for (std::uint32_t index = 0; index < std::min<std::uint32_t>(detections.count, inference::kMaxBoxes); ++index) {
            const auto &box = detections.boxes[index];
            if (!(box.confidence > confidence && box.confidence <= 1.0F) || box.width <= 0 || box.height <= 0)
                continue;
            const auto rectangle = Map(box.x - box.width / 4, box.y - box.height / 4,
                box.x + box.width + box.width / 4, box.y + box.height + box.height / 4, detection_mapping);
            if (rectangle.width == 0)
                continue;
            selected = rectangle;
            confidence = box.confidence;
        }
        return selected;
    }

    static Rect Foreground(const inference::SegmentationSet &segmentation, const Mapping &mapping)
    {
        if (segmentation.mask_width == 0 || segmentation.mask_height == 0
            || segmentation.mask_width > inference::kSegmentationMaskWidth
            || segmentation.mask_height > inference::kSegmentationMaskHeight)
            return {};
        std::uint32_t left = segmentation.mask_width, top = segmentation.mask_height, right = 0, bottom = 0, count = 0;
        for (std::uint32_t row = 0; row < segmentation.mask_height; ++row) {
            for (std::uint32_t column = 0; column < segmentation.mask_width; ++column) {
                if (segmentation.mask.data()[row * segmentation.mask_width + column] == 0)
                    continue;
                left = std::min(left, column);
                top = std::min(top, row);
                right = std::max(right, column + 1);
                bottom = std::max(bottom, row + 1);
                ++count;
            }
        }
        if (count < 4)
            return {};
        return Map(left * mapping.input_width / segmentation.mask_width, top * mapping.input_height / segmentation.mask_height,
            (right * mapping.input_width + segmentation.mask_width - 1) / segmentation.mask_width,
            (bottom * mapping.input_height + segmentation.mask_height - 1) / segmentation.mask_height, mapping);
    }

    inference::BoxSet latest_{};
    Values values_{};
    std::uint32_t face_tick_ = 0, person_tick_ = 0, foreground_tick_ = 0;
    std::uint32_t face_sequence_ = 0, person_sequence_ = 0, foreground_sequence_ = 0;
    bool have_face_ = false, have_person_ = false, have_foreground_ = false;
};

}