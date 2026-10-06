#include "middleware/ui/widget.hpp"

namespace uai::ai::ui {

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
        canvas.FillRect(button.bounds,
                        pressed ? button.style.pressed_fill : button.style.fill);
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

} // namespace uai::ai::ui
