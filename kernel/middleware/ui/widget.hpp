#pragma once

#include <cstddef>
#include <cstdint>

#include "middleware/ui/canvas.hpp"
#include "middleware/ui/touch_point.hpp"

namespace uai::ai::ui {

struct ButtonStyle {
    std::uint16_t fill = Rgb565(0x20U, 0x60U, 0xC0U);
    std::uint16_t pressed_fill = Rgb565(0x10U, 0x30U, 0x60U);
    std::uint16_t checked_fill = Rgb565(0x00U, 0xA0U, 0x60U);
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

enum class TextAlign : std::uint8_t {
    kLeft,
    kCenter,
    kRight,
};

struct LabelStyle {
    std::uint16_t text = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint16_t fill = Rgb565(0x00U, 0x00U, 0x00U);
    bool has_fill = true;
    std::uint8_t text_scale = 2U;
    TextAlign align = TextAlign::kLeft;
    std::uint8_t padding = 4U;
};

/* A text field whose content the application replaces at run time. */
struct LabelSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    const char *text = "";
    LabelStyle style{};
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

inline constexpr std::size_t kMaxButtons = 16U;

class ButtonPanel final : public Painter {
public:
    ButtonPanel(const ButtonSpec *buttons, std::size_t count)
        : buttons_(buttons),
          count_(buttons != nullptr && count <= kMaxButtons ? count : 0U) {}

    /* Feed one touch sample per poll. kPress is returned on the touch-down
     * that lands on a button, kTap on the following release. */
    Event Update(const TouchPoint &sample);
    void Paint(Canvas &canvas) const override;
    bool IsPressed(std::uint16_t id) const;
    /* Checked buttons draw with checked_fill; the application decides what
     * checked means (toggle, radio group, ...). */
    void SetChecked(std::uint16_t id, bool checked);
    bool IsChecked(std::uint16_t id) const;
    std::size_t Count() const { return count_; }

private:
    std::int32_t IndexOf(std::uint16_t id) const;
    const ButtonSpec *buttons_;
    std::size_t count_;
    std::int32_t pressed_index_ = -1;
    bool touch_active_ = false;
    bool checked_[kMaxButtons] = {};
};

inline constexpr std::size_t kMaxLabels = 8U;
inline constexpr std::size_t kLabelTextCapacity = 64U;

class LabelPanel final : public Painter {
public:
    LabelPanel(const LabelSpec *labels, std::size_t count);

    /* Text longer than kLabelTextCapacity - 1 is truncated. Returns false for
     * an unknown id. */
    bool SetText(std::uint16_t id, const char *text);
    const char *Text(std::uint16_t id) const;
    void Paint(Canvas &canvas) const override;
    std::size_t Count() const { return count_; }

private:
    std::int32_t IndexOf(std::uint16_t id) const;
    const LabelSpec *labels_;
    std::size_t count_;
    char text_[kMaxLabels][kLabelTextCapacity] = {};
};

/* Paints several painters in order so the LCD driver takes one overlay. */
class PainterGroup final : public Painter {
public:
    PainterGroup(const Painter *const *painters, std::size_t count)
        : painters_(painters), count_(painters != nullptr ? count : 0U) {}
    void Paint(Canvas &canvas) const override;

private:
    const Painter *const *painters_;
    std::size_t count_;
};

} // namespace uai::ai::ui
