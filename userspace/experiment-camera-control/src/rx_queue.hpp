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
        data_[write % 256] = {value, pending_error_};
        pending_error_ = false;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool Pop(char &value, bool &receive_error)
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) { return false; }
        const auto sample = data_[read % 256];
        value = sample.value;
        receive_error = sample.error;
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    bool Empty() const { return read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire); }
    void Error() { pending_error_ = true; }
private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    struct Sample { char value; bool error; };
    Sample data_[256]{};
    std::atomic<std::uint32_t> read_{0}, write_{0};
    bool pending_error_ = false;
};

}