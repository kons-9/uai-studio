#pragma once
#include "staging.hpp"

namespace experiment::model {

enum class Progress { kRunning, kDone, kError };

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool Start(const Manifest &manifest) = 0;
    virtual Progress Poll() = 0;
    virtual const std::uint8_t *Output(std::size_t &bytes) = 0;
    virtual bool Stop() = 0;
};

class Execution {
public:
    enum class State { kIdle, kRunning, kDone, kError };

    Execution(Staging &staging, Backend &backend) : staging_(staging), backend_(backend) {}

    bool Run(std::uint32_t now)
    {
        if (staging_.InUse()) {
            return false;
        }
        elapsed_ = 0;
        crc_ = 0;
        if (!staging_.Reverify() || !staging_.Acquire()) {
            state_ = State::kError;
            return false;
        }
        start_ = now;
        state_ = State::kRunning;
        if (!backend_.Start(*staging_.Verified())) {
            Finish(State::kError);
            return false;
        }
        return true;
    }

    bool Tick(std::uint32_t now)
    {
        if (state_ != State::kRunning) {
            return false;
        }
        elapsed_ = now - start_;
        if (elapsed_ >= 5000) {
            Finish(State::kError);
            return true;
        }
        const auto progress = backend_.Poll();
        if (progress == Progress::kRunning) {
            return false;
        }
        if (progress == Progress::kError) {
            Finish(State::kError);
            return true;
        }
        std::size_t bytes = 0;
        const auto *output = backend_.Output(bytes);
        if (!output || bytes != staging_.Verified()->output_bytes) {
            Finish(State::kError);
            return true;
        }
        crc_ = Crc32(output, bytes);
        Finish(State::kDone);
        return true;
    }

    bool Reset()
    {
        if (staging_.InUse()) {
            if (!backend_.Stop()) {
                state_ = State::kError;
                return false;
            }
            staging_.Release();
        }
        state_ = State::kIdle;
        crc_ = 0;
        elapsed_ = 0;
        return true;
    }

    State Status() const { return state_; }
    std::uint32_t Crc() const { return crc_; }
    std::uint32_t Elapsed() const { return elapsed_; }

    const char *Name() const
    {
        switch (state_) {
        case State::kRunning: return "running";
        case State::kDone: return "done";
        case State::kError: return "faulted";
        default: return staging_.Ready() ? "ready" : "waiting";
        }
    }

private:
    void Finish(State state)
    {
        if (backend_.Stop()) {
            staging_.Release();
            state_ = state;
        } else {
            state_ = State::kError;
        }
        if (state_ == State::kError) {
            crc_ = 0;
        }
    }

    Staging &staging_;
    Backend &backend_;
    State state_ = State::kIdle;
    std::uint32_t start_ = 0, elapsed_ = 0, crc_ = 0;
};

}