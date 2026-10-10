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

/* Built-in glyph-free icons drawn instead of the label when set. */
enum class Icon : std::uint8_t {
    kNone,
    kMenu,  /* three bars (hamburger) */
    kBack,  /* left arrow */
    kClose, /* cross */
};

/* Static description of one button. Tables of these are the contract that
 * host_app/ui_designer generates; runtime state lives in ButtonPanel. */
struct ButtonSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    const char *label = "";
    ButtonStyle style{};
    Icon icon = Icon::kNone;
    Shape shape = Shape::kRectangle;

    constexpr bool Contains(
        std::uint16_t x,
        std::uint16_t y
    ) const
    {
        return InsideShape(shape, bounds, x, y);
    }
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

struct SliderStyle {
    std::uint16_t track = Rgb565(0x40U, 0x40U, 0x40U);
    std::uint16_t fill = Rgb565(0x20U, 0x60U, 0xC0U);
    std::uint16_t knob = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint16_t text = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint8_t text_scale = 2U;
    bool show_value = true;
};

/* Horizontal slider. The caption and current value are drawn above the
 * track; the knob follows the finger while it stays down. */
struct SliderSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    const char *label = "";
    std::int32_t minimum = 0;
    std::int32_t maximum = 100;
    std::int32_t step = 1;
    std::int32_t initial = 0;
    SliderStyle style{};
};

struct DialStyle {
    std::uint16_t face = Rgb565(0x20U, 0x28U, 0x30U);
    std::uint16_t track = Rgb565(0x40U, 0x40U, 0x40U);
    std::uint16_t fill = Rgb565(0x20U, 0x60U, 0xC0U);
    std::uint16_t pointer = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint16_t text = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint8_t text_scale = 2U;
    bool show_value = true;
};

/* Rotary knob: a 270-degree arc from bottom-left to bottom-right. The
 * caption and current value are drawn above the dial like the slider. */
struct DialSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    const char *label = "";
    std::int32_t minimum = 0;
    std::int32_t maximum = 100;
    std::int32_t step = 1;
    std::int32_t initial = 0;
    DialStyle style{};
};

struct WheelStyle {
    std::uint16_t fill = Rgb565(0x18U, 0x20U, 0x28U);
    std::uint16_t highlight = Rgb565(0x20U, 0x60U, 0xC0U);
    std::uint16_t text = Rgb565(0x80U, 0x90U, 0xA0U);
    std::uint16_t selected_text = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint16_t border = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint8_t text_scale = 2U;
};

/* Vertical picker: the selected item sits in a highlighted band in the
 * middle, neighbours above and below; dragging up or down steps through
 * the items. Event::value is the selected index. */
struct WheelSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    const char *const *items = nullptr;
    std::size_t item_count = 0U;
    std::size_t initial = 0U;
    WheelStyle style{};
};

struct NumberStyle {
    std::uint16_t text = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint16_t fill = Rgb565(0x00U, 0x00U, 0x00U);
    bool has_fill = true;
    std::uint8_t text_scale = 4U;
    TextAlign align = TextAlign::kRight;
};

/* Numeric read-out set by the application. `decimals` places the point:
 * value 1234 with decimals 1 shows "123.4"; `unit` is appended. */
struct NumberSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    const char *label = "";
    const char *unit = "";
    std::uint8_t decimals = 0U;
    std::int32_t initial = 0;
    NumberStyle style{};
};

/* RGB565 bitmap generated from a PNG by host_app/ui_designer. The bitmap
 * dimensions equal the bounds; `transparent` pixels are skipped. */
struct ImageSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    const std::uint16_t *pixels = nullptr;
    bool has_transparent = false;
    std::uint16_t transparent = 0U;
};

struct PadStyle {
    std::uint16_t fill = Rgb565(0x30U, 0x30U, 0x30U);
    std::uint16_t pressed_fill = Rgb565(0x60U, 0x60U, 0x60U);
    std::uint16_t center_fill = Rgb565(0x20U, 0x60U, 0xC0U);
    std::uint16_t border = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint16_t arrow = Rgb565(0xFFU, 0xFFU, 0xFFU);
    std::uint8_t border_width = 2U;
};

/* Segment of a Pad, reported in Event::value for kPress and kTap. */
enum class PadSegment : std::uint8_t {
    kUp = 0U,
    kRight = 1U,
    kDown = 2U,
    kLeft = 3U,
    kCenter = 4U,
};

/* Round four-way pad like a camera's rear control wheel or a game pad.
 * Tapping a quadrant (or the optional centre button) gives kTap with the
 * PadSegment; kChange carries signed 45-degree steps (positive clockwise). */
struct PadSpec {
    std::uint16_t id = 0U;
    Rect bounds{};
    bool has_center = true;
    PadStyle style{};
};

enum class EventType : std::uint8_t {
    kNone,
    kPress,
    kTap,
    kChange, /* slider value changed; Event::value holds the new value */
};

struct Event {
    EventType type = EventType::kNone;
    std::uint16_t widget_id = 0U;
    std::uint16_t x = 0U;
    std::uint16_t y = 0U;
    std::int32_t value = 0;
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
    ButtonPanel(
        const ButtonSpec *buttons,
        std::size_t count
    )
        : buttons_(buttons),
          count_(buttons != nullptr && count <= kMaxButtons ? count : 0U)
    {}

    /* Feed one touch sample per poll. kPress is returned on the touch-down
     * that lands on a button, kTap on the following release. */
    Event Update(const TouchPoint &sample);
    void Paint(Canvas &canvas) const override;
    bool IsPressed(std::uint16_t id) const;
    /* Checked buttons draw with checked_fill; the application decides what
     * checked means (toggle, radio group, ...). */
    void SetChecked(
        std::uint16_t id,
        bool checked
    );
    bool IsChecked(std::uint16_t id) const;
    void SetEnabled(
        std::uint16_t id,
        bool enabled
    );
    bool IsEnabled(std::uint16_t id) const;
    std::size_t Count() const { return count_; }

private:
    std::int32_t IndexOf(std::uint16_t id) const;
    const ButtonSpec *buttons_;
    std::size_t count_;
    std::int32_t pressed_index_ = -1;
    bool touch_active_ = false;
    bool checked_[kMaxButtons] = {};
    bool disabled_[kMaxButtons] = {};
};

inline constexpr std::size_t kMaxLabels = 8U;
inline constexpr std::size_t kLabelTextCapacity = 64U;

class LabelPanel final : public Painter {
public:
    LabelPanel(
        const LabelSpec *labels,
        std::size_t count
    );

    /* Text longer than kLabelTextCapacity - 1 is truncated. Returns false for
     * an unknown id. */
    bool SetText(
        std::uint16_t id,
        const char *text
    );
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
    PainterGroup(
        const Painter *const *painters,
        std::size_t count
    )
        : painters_(painters),
          count_(painters != nullptr ? count : 0U)
    {}
    void Paint(Canvas &canvas) const override;

private:
    const Painter *const *painters_;
    std::size_t count_;
};

inline constexpr std::size_t kMaxSliders = 8U;

class SliderPanel final : public Painter {
public:
    SliderPanel(
        const SliderSpec *sliders,
        std::size_t count
    );

    /* kChange is returned whenever the finger moves the value, including on
     * the initial press. */
    Event Update(const TouchPoint &sample);
    void Paint(Canvas &canvas) const override;
    std::int32_t Value(std::uint16_t id) const;
    bool SetValue(
        std::uint16_t id,
        std::int32_t value
    );
    bool IsDragging() const { return active_index_ >= 0; }
    std::size_t Count() const { return count_; }

    /* Track geometry inside the bounds; shared with the host preview. */
    static Rect TrackOf(const SliderSpec &slider);
    static std::int32_t ValueAt(
        const SliderSpec &slider,
        std::uint16_t x
    );

private:
    std::int32_t IndexOf(std::uint16_t id) const;
    static std::int32_t Clamp(
        const SliderSpec &slider,
        std::int32_t value
    );
    const SliderSpec *sliders_;
    std::size_t count_;
    std::int32_t values_[kMaxSliders] = {};
    std::int32_t active_index_ = -1;
    bool touch_active_ = false;
};

enum class Background : std::uint8_t {
    kCamera, /* widgets are drawn over the live Pipe1 frame */
    kSolid,  /* the frame is filled with `color` first */
};

inline constexpr std::size_t kMaxDials = 4U;

class DialPanel final : public Painter {
public:
    DialPanel(
        const DialSpec *dials,
        std::size_t count
    );

    Event Update(const TouchPoint &sample);
    void Paint(Canvas &canvas) const override;
    std::int32_t Value(std::uint16_t id) const;
    bool SetValue(
        std::uint16_t id,
        std::int32_t value
    );
    bool IsDragging() const { return active_index_ >= 0; }
    std::size_t Count() const { return count_; }

    /* Dial disc below the caption row; shared with the host preview. */
    static Rect DiscOf(const DialSpec &dial);
    /* Value for a finger at (x, y); points in the bottom gap clamp to the
     * nearer end. */
    static std::int32_t ValueAt(
        const DialSpec &dial,
        std::uint16_t x,
        std::uint16_t y
    );
    /* Clockwise degrees from the arc start (bottom-left) for a value. */
    static std::uint16_t SweepOf(
        const DialSpec &dial,
        std::int32_t value
    );

private:
    std::int32_t IndexOf(std::uint16_t id) const;
    static std::int32_t Clamp(
        const DialSpec &dial,
        std::int32_t value
    );
    const DialSpec *dials_;
    std::size_t count_;
    std::int32_t values_[kMaxDials] = {};
    std::int32_t active_index_ = -1;
    bool touch_active_ = false;
};

inline constexpr std::size_t kMaxWheels = 4U;

class WheelPanel final : public Painter {
public:
    WheelPanel(
        const WheelSpec *wheels,
        std::size_t count
    );

    Event Update(const TouchPoint &sample);
    void Paint(Canvas &canvas) const override;
    std::int32_t Value(std::uint16_t id) const;
    bool SetValue(
        std::uint16_t id,
        std::int32_t index
    );
    const char *ItemText(std::uint16_t id) const;
    std::size_t Count() const { return count_; }

    /* Height of one item row; dragging this far moves one item. */
    static std::uint16_t RowHeightOf(const WheelSpec &wheel);

private:
    std::int32_t IndexOf(std::uint16_t id) const;
    const WheelSpec *wheels_;
    std::size_t count_;
    std::int32_t selected_[kMaxWheels] = {};
    std::int32_t active_index_ = -1;
    std::int32_t anchor_selected_ = 0;
    std::uint16_t anchor_y_ = 0U;
    bool touch_active_ = false;
};

inline constexpr std::size_t kMaxNumbers = 8U;
inline constexpr std::size_t kNumberTextCapacity = 24U;

class NumberPanel final : public Painter {
public:
    NumberPanel(
        const NumberSpec *numbers,
        std::size_t count
    );

    bool SetValue(
        std::uint16_t id,
        std::int32_t value
    );
    std::int32_t Value(std::uint16_t id) const;
    void Paint(Canvas &canvas) const override;
    std::size_t Count() const { return count_; }

    /* Formats `value` with the spec's decimals and unit into `out`. */
    static void Format(
        const NumberSpec &number,
        std::int32_t value,
        char (&out)[kNumberTextCapacity]
    );

private:
    std::int32_t IndexOf(std::uint16_t id) const;
    const NumberSpec *numbers_;
    std::size_t count_;
    std::int32_t values_[kMaxNumbers] = {};
};

class ImagePanel final : public Painter {
public:
    ImagePanel(
        const ImageSpec *images,
        std::size_t count
    )
        : images_(images),
          count_(images != nullptr ? count : 0U)
    {}
    void Paint(Canvas &canvas) const override;
    std::size_t Count() const { return count_; }

private:
    const ImageSpec *images_;
    std::size_t count_;
};

inline constexpr std::size_t kMaxPads = 2U;
inline constexpr std::int32_t kPadDetentDegrees = 45;

class PadPanel final : public Painter {
public:
    PadPanel(
        const PadSpec *pads,
        std::size_t count
    )
        : pads_(pads),
          count_(pads != nullptr && count <= kMaxPads ? count : 0U)
    {}

    Event Update(const TouchPoint &sample);
    void Paint(Canvas &canvas) const override;
    /* Segment currently held, or -1. */
    std::int32_t Pressed(std::uint16_t id) const;
    std::size_t Count() const { return count_; }

    /* Centre button disc; shared with the host preview. */
    static Rect CenterOf(const PadSpec &pad);
    /* PadSegment under (x, y) as an int, or -1 outside the pad. */
    static std::int32_t SegmentAt(
        const PadSpec &pad,
        std::uint16_t x,
        std::uint16_t y
    );
    /* Clockwise degrees from the top for (x, y) relative to the pad centre. */
    static std::int32_t AngleAt(
        const PadSpec &pad,
        std::uint16_t x,
        std::uint16_t y
    );

private:
    const PadSpec *pads_;
    std::size_t count_;
    std::int32_t active_index_ = -1;
    std::int32_t pressed_segment_ = -1;
    std::int32_t last_angle_ = 0;
    std::int32_t accumulated_ = 0;
    std::uint16_t last_x_ = 0U;
    std::uint16_t last_y_ = 0U;
    bool rotated_ = false;
    bool touch_active_ = false;
};

/* One page of the UI. Tables of these are generated; Screen holds state. */
struct ScreenSpec {
    std::uint16_t id = 0U;
    Background background = Background::kCamera;
    std::uint16_t color = 0U;
    const ButtonSpec *buttons = nullptr;
    std::size_t button_count = 0U;
    const LabelSpec *labels = nullptr;
    std::size_t label_count = 0U;
    const SliderSpec *sliders = nullptr;
    std::size_t slider_count = 0U;
    const DialSpec *dials = nullptr;
    std::size_t dial_count = 0U;
    const WheelSpec *wheels = nullptr;
    std::size_t wheel_count = 0U;
    const NumberSpec *numbers = nullptr;
    std::size_t number_count = 0U;
    const ImageSpec *images = nullptr;
    std::size_t image_count = 0U;
    const PadSpec *pads = nullptr;
    std::size_t pad_count = 0U;
};

class Screen final : public Painter {
public:
    explicit Screen(const ScreenSpec &spec);

    /* Routes the sample to the touch-sensitive panels of this screen. */
    Event Update(const TouchPoint &sample);
    /* Fills a solid background when configured, then draws images,
     * buttons, sliders, dials, wheels, pads, numbers, and labels in that
     * order. */
    void Paint(Canvas &canvas) const override;

    const ScreenSpec &Spec() const { return spec_; }
    ButtonPanel &Buttons() { return buttons_; }
    LabelPanel &Labels() { return labels_; }
    SliderPanel &Sliders() { return sliders_; }
    DialPanel &Dials() { return dials_; }
    WheelPanel &Wheels() { return wheels_; }
    NumberPanel &Numbers() { return numbers_; }
    PadPanel &Pads() { return pads_; }
    const ButtonPanel &Buttons() const { return buttons_; }
    const LabelPanel &Labels() const { return labels_; }
    const SliderPanel &Sliders() const { return sliders_; }
    const DialPanel &Dials() const { return dials_; }
    const WheelPanel &Wheels() const { return wheels_; }
    const NumberPanel &Numbers() const { return numbers_; }
    const PadPanel &Pads() const { return pads_; }

private:
    const ScreenSpec &spec_;
    ButtonPanel buttons_;
    LabelPanel labels_;
    SliderPanel sliders_;
    DialPanel dials_;
    WheelPanel wheels_;
    NumberPanel numbers_;
    ImagePanel images_;
    PadPanel pads_;
};

} // namespace uai::ai::ui
