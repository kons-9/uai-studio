#pragma once

#include <atomic>
#include <cstdint>
#include "middleware/task/event_notification.hpp"

namespace uai::ai::shell {

enum class Action : std::uint8_t {
    kCameraStatus,
    kCameraAe,
    kCameraCompensation,
    kCameraManual,
    kCameraStatistics,
    kCameraFps,
    kCameraFlip,
    kCameraCrop,
    kModels,
    kBoxes,
    kAiExposure,
    kUiStatus,
    kDiagnostics,
};

struct Request {
    Action action = Action::kCameraStatus;
    std::int32_t values[4]{};
};

struct Reply {
    std::int32_t code = 0;
    char text[512]{};
};

class Mailbox final {
public:
    void SetNotification(common::EventNotification notification) { notification_ = notification; }
    bool Pending() const { return phase_.load(std::memory_order_acquire) == Phase::kPending; }
    bool Post(const Request &request)
    {
        if (phase_.load(std::memory_order_acquire) != Phase::kIdle)
            return false;
        request_ = request;
        phase_.store(Phase::kPending, std::memory_order_release);
        notification_.Notify();
        return true;
    }

    bool Take(Request *request)
    {
        Phase pending = Phase::kPending;
        if (!phase_.compare_exchange_strong(pending, Phase::kRunning, std::memory_order_acquire))
            return false;
        *request = request_;
        return true;
    }

    void Complete(const Reply &reply)
    {
        reply_ = reply;
        phase_.store(Phase::kDone, std::memory_order_release);
    }

    bool Receive(Reply *reply)
    {
        if (phase_.load(std::memory_order_acquire) != Phase::kDone)
            return false;
        *reply = reply_;
        phase_.store(Phase::kIdle, std::memory_order_release);
        return true;
    }

private:
    enum class Phase : std::uint8_t {
        kIdle,
        kPending,
        kRunning,
        kDone
    };
    Request request_{};
    Reply reply_{};
    std::atomic<Phase> phase_{Phase::kIdle};
    common::EventNotification notification_;
};

} // namespace uai::ai::shell