#pragma once
#include <atomic>
#include <cstdint>

namespace experiment::console {

class RxQueue {
public:
    bool Push(char value)
    {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) == 256) { Error(); return false; }
        data_[write % 256] = value;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool Pop(char &value)
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) { return false; }
        value = data_[read % 256];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    bool Empty() const { return read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire); }
    void Error() { errors_.store(true, std::memory_order_release); }
    bool TakeError() { return errors_.exchange(false, std::memory_order_acq_rel); }
private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
    char data_[256]{};
    std::atomic<std::uint32_t> read_{0}, write_{0};
    std::atomic<bool> errors_{false};
};

}