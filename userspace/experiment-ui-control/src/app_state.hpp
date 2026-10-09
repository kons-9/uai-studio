#pragma once
#include "features_generated.hpp"
#include "camera_control.hpp"
#include "periodic.hpp"

namespace experiment::ui {

struct Result {
    std::uint32_t sequence = 0, expires = 0;
    camera::Rect bounds{};
    bool valid = false;
};
struct FrameContext { std::uint32_t milliseconds, sequence; };
enum class TransactionStatus { kIdle, kCommitted, kRejected, kRolledBack, kFaulted };
struct RenderTaskState {
    int pressed = -1;
    bool error = false, faulted = false;
    std::uint32_t updates = 0;
    TransactionStatus transaction = TransactionStatus::kIdle;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool Apply(features::State state) = 0;
};

class AppState {
public:
    explicit AppState(Backend &backend) : backend_(backend) {}
    bool Dispatch(features::Action action)
    {
        return Transact(&action, 1);
    }
    bool Transact(const features::Action *actions, std::size_t count)
    {
        if (render_.faulted) { return false; }
        render_.error = false;
        render_.transaction = TransactionStatus::kRejected;
        if (!actions || count == 0) { return false; }
        auto next = state_;
        for (std::size_t index = 0; index < count; ++index) {
            if (!features::Apply(next, actions[index])) { return false; }
        }
        if (!backend_.Apply(next)) {
            render_.error = true;
            render_.faulted = !backend_.Apply(state_);
            render_.transaction = render_.faulted ? TransactionStatus::kFaulted : TransactionStatus::kRolledBack;
            return false;
        }
        state_ = next; ++render_.updates;
        render_.transaction = TransactionStatus::kCommitted;
        return true;
    }
    bool Recover()
    {
        if (!render_.faulted) { return true; }
        if (!backend_.Apply(state_)) { return false; }
        render_.faulted = false; render_.error = false;
        render_.transaction = TransactionStatus::kIdle;
        return true;
    }
    bool Publish(FrameContext frame, camera::Rect rectangle, std::uint32_t lifetime)
    {
        if (lifetime == 0 || lifetime > INT32_MAX || !camera::Inside(rectangle, 400, 480) ||
            (published_ && static_cast<std::int32_t>(frame.sequence - result_.sequence) <= 0)) { return false; }
        result_ = {frame.sequence, frame.milliseconds + lifetime, rectangle, true}; published_ = true;
        return true;
    }
    const Result *Visible(std::uint32_t now)
    {
        if (result_.valid && static_cast<std::int32_t>(now - result_.expires) >= 0) { result_.valid = false; }
        return !render_.faulted && result_.valid && std::strcmp(features::Value(state_, 1), "on") == 0 ? &result_ : nullptr;
    }
    bool Touch(bool down, int hit)
    {
        if (down) {
            if (!down_) { render_.pressed = hit; }
            else if (hit != render_.pressed) { render_.pressed = -1; }
            down_ = true; return false;
        }
        const auto selected = render_.pressed;
        render_.pressed = -1;
        const bool clicked = down_ && selected >= 0 && selected == hit;
        down_ = false;
        return clicked && Dispatch(static_cast<features::Action>(selected));
    }
    features::State Features() const { return state_; }
    const RenderTaskState &Render() const { return render_; }
private:
    Backend &backend_;
    features::State state_{};
    RenderTaskState render_{};
    Result result_{};
    bool published_ = false, down_ = false;
};

}