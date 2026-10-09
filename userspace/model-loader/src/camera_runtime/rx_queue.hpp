#pragma once
#include <atomic>
#include <cstdint>

namespace experiment::console {

class RxQueue {
public:
    static constexpr std::uint32_t kCapacity = 2048;

    bool Push(char value)
    {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) >= kCapacity) {
            Error();
            return false;
        }
        data_[write % kCapacity] = {value, pending_error_};
        pending_error_ = false;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool
    Pop(char &value,
        bool &receive_error)
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) {
            return false;
        }
        const auto sample = data_[read % kCapacity];
        value = sample.value;
        receive_error = sample.error;
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    bool Empty() const { return read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire); }
    void Error() { pending_error_ = true; }

private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    struct Sample {
        char value;
        bool error;
    };
    Sample data_[kCapacity]{};
    std::atomic<std::uint32_t> read_{0}, write_{0};
    bool pending_error_ = false;
};

}
