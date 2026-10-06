#pragma once

#include <cstddef>
#include <cstdint>

#include "middleware/ui/canvas.hpp"

namespace uai::ai::ui {

struct TouchPoint {
    bool active = false;
    std::uint16_t x = 0U;
    std::uint16_t y = 0U;
};

struct ButtonStyle {
    std::uint16_t fill = Rgb565(0x20U, 0x60U, 0xC0U);
    std::uint16_t pressed_fill = Rgb565(0x10U, 0x30U, 0x60U);
    std::uint16_t border = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint16_t text = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint8_t text_scale = 3U;
    std::uint8_t border_width = 2U;
};

/* Static description of one button. Tables of these are the contract that
 * host_app/ui_designer generates; runtime state lives in ButtonPanel. */
struct ButtonSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    const char *label = "";
    ButtonStyle style{};
};

enum class EventType : std::uint8_t {
    kNone,
    kPress,
    kTap,
};

struct Event {
    EventType type = EventType::kNone;
    std::uint16_t widget_id = 0U;
    std::uint16_t x = 0U;
    std::uint16_t y = 0U;
};

/* Something that draws itself onto a frame already holding the camera image. */
class Painter {
public:
    virtual void Paint(Canvas &canvas) const = 0;

protected:
    ~Painter() = default;
};

class ButtonPanel final : public Painter {
public:
    ButtonPanel(const ButtonSpec *buttons, std::size_t count)
        : buttons_(buttons), count_(buttons != nullptr ? count : 0U) {}

    /* Feed one touch sample per poll. kPress is returned on the touch-down
     * that lands on a button, kTap on the following release. */
    Event Update(const TouchPoint &sample);
    void Paint(Canvas &canvas) const override;
    bool IsPressed(std::uint16_t id) const;
    std::size_t Count() const { return count_; }

private:
    const ButtonSpec *buttons_;
    std::size_t count_;
    std::int32_t pressed_index_ = -1;
    bool touch_active_ = false;
};

} // namespace uai::ai::ui
