#pragma once

#include "shell.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>

namespace experiment::camera {

struct Rect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

inline bool Inside(
    const Rect &rectangle,
    std::uint32_t width,
    std::uint32_t height
)
{
    return rectangle.width != 0 && rectangle.height != 0 && rectangle.x < width && rectangle.y < height
        && rectangle.width <= width - rectangle.x && rectangle.height <= height - rectangle.y;
}

inline bool MapToSensor(
    const Rect &input,
    const Rect &content,
    const Rect &crop,
    Rect &output
)
{
    constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
    if (!Inside(input, limit, limit) || !Inside(content, limit, limit) || !Inside(crop, limit, limit)) {
        return false;
    }
    const auto left = input.x > content.x ? input.x : content.x;
    const auto top = input.y > content.y ? input.y : content.y;
    const auto right =
        input.x + input.width < content.x + content.width ? input.x + input.width : content.x + content.width;
    const auto bottom =
        input.y + input.height < content.y + content.height ? input.y + input.height : content.y + content.height;
    if (right <= left || bottom <= top) {
        return false;
    }
    const auto mapped_left = static_cast<std::uint32_t>(std::uint64_t(left - content.x) * crop.width / content.width);
    const auto mapped_top = static_cast<std::uint32_t>(std::uint64_t(top - content.y) * crop.height / content.height);
    const auto mapped_right =
        static_cast<std::uint32_t>((std::uint64_t(right - content.x) * crop.width + content.width - 1) / content.width);
    const auto mapped_bottom = static_cast<std::uint32_t>(
        (std::uint64_t(bottom - content.y) * crop.height + content.height - 1) / content.height
    );
    output = {crop.x + mapped_left, crop.y + mapped_top, mapped_right - mapped_left, mapped_bottom - mapped_top};
    return true;
}

struct State {
    bool auto_exposure = true;
    int compensation = 0;
    std::uint32_t target = 0;
    std::int32_t reported_exposure_us = 0;
    std::int32_t reported_gain_mdB = 0;
    bool auto_white_balance = true;
    std::uint32_t color_temperature = 0;
    Rect statistics;
    std::uint32_t sensor_width = 0;
    std::uint32_t sensor_height = 0;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual console::Status Read(State &state) = 0;
    virtual console::Status AutoExposure(bool enabled) = 0;
    virtual console::Status Compensation(int half_stops) = 0;
    virtual console::Status Manual(
        std::int32_t exposure_us,
        std::int32_t gain_mdB
    ) = 0;
    virtual console::Status Statistics(Rect rectangle) = 0;
    virtual console::Status WhiteBalance(std::uint32_t temperature) = 0;
    virtual console::Status ListWhiteBalance(const console::Writer &writer) = 0;
};

inline bool ParseInteger(
    const char *text,
    std::int32_t &value
)
{
    const bool negative = *text == '-';
    if (negative) {
        ++text;
    }
    if (*text == '\0') {
        return false;
    }
    const std::uint32_t limit = negative ? 2147483648U : 2147483647U;
    std::uint32_t magnitude = 0;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9') {
            return false;
        }
        const auto digit = static_cast<std::uint32_t>(*text - '0');
        if (magnitude > (limit - digit) / 10) {
            return false;
        }
        magnitude = magnitude * 10 + digit;
    }
    value = static_cast<std::int32_t>(negative ? -static_cast<std::int64_t>(magnitude) : magnitude);
    return true;
}

inline console::Status Execute(
    void *context,
    int count,
    const char *const *arguments,
    const console::Writer &writer
)
{
    auto &backend = *static_cast<Backend *>(context);
    if (count < 2) {
        return console::Status::kInvalidArgument;
    }
    State state;
    auto status = backend.Read(state);
    if (status != console::Status::kOk) {
        return status;
    }
    if (std::strcmp(arguments[1], "stat") == 0 && count == 2) {
        char text[256];
        std::snprintf(
            text,
            sizeof(text),
            "cam: ae=%u ev_half=%d target=%lu reported_us=%ld reported_mdB=%ld wb_auto=%u wb_kelvin=%lu "
            "area=%lu,%lu,%lu,%lu\n",
            static_cast<unsigned>(state.auto_exposure),
            state.compensation,
            static_cast<unsigned long>(state.target),
            static_cast<long>(state.reported_exposure_us),
            static_cast<long>(state.reported_gain_mdB),
            static_cast<unsigned>(state.auto_white_balance),
            static_cast<unsigned long>(state.color_temperature),
            static_cast<unsigned long>(state.statistics.x),
            static_cast<unsigned long>(state.statistics.y),
            static_cast<unsigned long>(state.statistics.width),
            static_cast<unsigned long>(state.statistics.height)
        );
        writer.Write(text);
        return console::Status::kOk;
    }
    if (std::strcmp(arguments[1], "wb-list") == 0 && count == 2) {
        return backend.ListWhiteBalance(writer);
    }
    std::int32_t values[4]{};
    if (std::strcmp(arguments[1], "ae") == 0 && count == 3) {
        if (std::strcmp(arguments[2], "on") != 0 && std::strcmp(arguments[2], "off") != 0) {
            return console::Status::kInvalidArgument;
        }
        status = backend.AutoExposure(std::strcmp(arguments[2], "on") == 0);
    } else if (std::strcmp(arguments[1], "ev") == 0 && count == 3) {
        if (!ParseInteger(arguments[2], values[0]) || values[0] < -4 || values[0] > 4) {
            return console::Status::kInvalidArgument;
        }
        status = backend.Compensation(values[0]);
    } else if (std::strcmp(arguments[1], "manual") == 0 && count == 4) {
        if (!ParseInteger(arguments[2], values[0]) || !ParseInteger(arguments[3], values[1]) || values[0] < 100
            || values[0] > 30000 || values[1] < 0 || values[1] > 24000) {
            return console::Status::kInvalidArgument;
        }
        if (state.auto_exposure) {
            return console::Status::kInvalidState;
        }
        status = backend.Manual(values[0], values[1]);
    } else if (std::strcmp(arguments[1], "area") == 0 && count == 6) {
        for (int index = 0; index < 4; ++index) {
            if (!ParseInteger(arguments[index + 2], values[index]) || values[index] < 0) {
                return console::Status::kInvalidArgument;
            }
        }
        const Rect rectangle{
            static_cast<std::uint32_t>(values[0]),
            static_cast<std::uint32_t>(values[1]),
            static_cast<std::uint32_t>(values[2]),
            static_cast<std::uint32_t>(values[3])
        };
        if (!Inside(rectangle, state.sensor_width, state.sensor_height)) {
            return console::Status::kInvalidArgument;
        }
        status = backend.Statistics(rectangle);
    } else if (std::strcmp(arguments[1], "wb") == 0 && count == 3) {
        if (std::strcmp(arguments[2], "auto") != 0 && (!ParseInteger(arguments[2], values[0]) || values[0] <= 0)) {
            return console::Status::kInvalidArgument;
        }
        status = backend.WhiteBalance(static_cast<std::uint32_t>(values[0]));
    } else {
        return console::Status::kInvalidArgument;
    }
    if (status == console::Status::kOk) {
        writer.Write("OK applied\n");
    }
    return status;
}

}