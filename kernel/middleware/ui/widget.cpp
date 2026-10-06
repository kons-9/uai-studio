#include "middleware/ui/widget.hpp"

#include <cstring>

namespace uai::ai::ui {

std::int32_t ButtonPanel::IndexOf(std::uint16_t id) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (buttons_[index].id == id) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

Event ButtonPanel::Update(const TouchPoint &sample)
{
    Event event{};
    const bool was_active = touch_active_;
    touch_active_ = sample.active;

    if (sample.active && !was_active) {
        for (std::size_t index = 0U; index < count_; ++index) {
            if (buttons_[index].bounds.Contains(sample.x, sample.y)) {
                pressed_index_ = static_cast<std::int32_t>(index);
                event.type = EventType::kPress;
                event.widget_id = buttons_[index].id;
                event.x = sample.x;
                event.y = sample.y;
                return event;
            }
        }
        return event;
    }

    if (!sample.active && was_active && pressed_index_ >= 0) {
        event.type = EventType::kTap;
        event.widget_id = buttons_[static_cast<std::size_t>(pressed_index_)].id;
        pressed_index_ = -1;
    }
    return event;
}

void ButtonPanel::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const ButtonSpec &button = buttons_[index];
        const bool pressed = static_cast<std::int32_t>(index) == pressed_index_;
        const std::uint16_t fill = pressed ? button.style.pressed_fill
                                   : checked_[index] ? button.style.checked_fill
                                                     : button.style.fill;
        canvas.FillRect(button.bounds, fill);
        canvas.DrawFrame(button.bounds, button.style.border_width,
                         button.style.border);
        canvas.DrawTextCentered(button.bounds, button.label,
                                button.style.text_scale, button.style.text);
    }
}

bool ButtonPanel::IsPressed(std::uint16_t id) const
{
    return pressed_index_ >= 0 &&
           buttons_[static_cast<std::size_t>(pressed_index_)].id == id;
}

void ButtonPanel::SetChecked(std::uint16_t id, bool checked)
{
    const std::int32_t index = IndexOf(id);
    if (index >= 0) {
        checked_[static_cast<std::size_t>(index)] = checked;
    }
}

bool ButtonPanel::IsChecked(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    return index >= 0 && checked_[static_cast<std::size_t>(index)];
}

LabelPanel::LabelPanel(const LabelSpec *labels, std::size_t count)
    : labels_(labels),
      count_(labels != nullptr && count <= kMaxLabels ? count : 0U)
{
    for (std::size_t index = 0U; index < count_; ++index) {
        std::strncpy(text_[index], labels_[index].text, kLabelTextCapacity - 1U);
    }
}

std::int32_t LabelPanel::IndexOf(std::uint16_t id) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (labels_[index].id == id) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

bool LabelPanel::SetText(std::uint16_t id, const char *text)
{
    const std::int32_t index = IndexOf(id);
    if (index < 0 || text == nullptr) {
        return false;
    }
    char *slot = text_[static_cast<std::size_t>(index)];
    std::strncpy(slot, text, kLabelTextCapacity - 1U);
    slot[kLabelTextCapacity - 1U] = '\0';
    return true;
}

const char *LabelPanel::Text(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    return index < 0 ? "" : text_[static_cast<std::size_t>(index)];
}

void LabelPanel::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const LabelSpec &label = labels_[index];
        const LabelStyle &style = label.style;
        if (style.has_fill) {
            canvas.FillRect(label.bounds, style.fill);
        }
        const char *text = text_[index];
        const std::uint32_t text_width = TextWidth(text, style.text_scale);
        const std::uint32_t text_height =
            static_cast<std::uint32_t>(kGlyphHeight) * style.text_scale;
        const std::uint32_t padding =
            2U * style.padding < label.bounds.width ? style.padding : 0U;
        const std::uint32_t inner_width = label.bounds.width - 2U * padding;
        std::uint32_t x = label.bounds.x + padding;
        if (text_width < inner_width) {
            if (style.align == TextAlign::kCenter) {
                x += (inner_width - text_width) / 2U;
            } else if (style.align == TextAlign::kRight) {
                x += inner_width - text_width;
            }
        }
        const std::uint32_t y = text_height < label.bounds.height
            ? label.bounds.y + (label.bounds.height - text_height) / 2U
            : label.bounds.y;
        canvas.DrawText(static_cast<std::uint16_t>(x),
                        static_cast<std::uint16_t>(y), text,
                        style.text_scale, style.text);
    }
}

void PainterGroup::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (painters_[index] != nullptr) {
            painters_[index]->Paint(canvas);
        }
    }
}

} // namespace uai::ai::ui
