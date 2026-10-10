#pragma once
#include "staging.hpp"

namespace experiment::model {

enum class Progress {
    kRunning,
    kDone,
    kError
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool Start(const Manifest &manifest) = 0;
    virtual Progress Poll() = 0;
    virtual const std::uint8_t *Output(std::size_t &bytes) = 0;
    virtual bool Stop() = 0;
    virtual std::uint8_t *Input(std::size_t &bytes)
    {
        bytes = 0;
        return nullptr;
    }
    virtual bool AdoptInput(std::size_t) { return false; }
};

class Execution {
public:
    enum class State {
        kIdle,
        kRunning,
        kDone,
        kError
    };

    Execution(
        Staging &staging,
        Backend &backend
    )
        : staging_(staging),
          backend_(backend)
    {}

    bool Run(std::uint32_t now)
    {
        if (staging_.InUse()) {
            return false;
        }
        elapsed_ = 0;
        crc_ = 0;
        result_ = nullptr;
        result_bytes_ = 0;
        if (staging_.Verified() && staging_.Verified()->tag && !input_ready_) {
            return false;
        }
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
        result_ = output;
        result_bytes_ = bytes;
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
        input_ready_ = false;
        input_receiving_ = false;
        input_offset_ = 0;
        result_ = nullptr;
        result_bytes_ = 0;
        return true;
    }

    bool InputBegin(
        std::uint32_t bytes,
        std::uint32_t crc
    )
    {
        if (staging_.InUse() || !staging_.Verified() || bytes != staging_.Verified()->input_bytes) {
            return false;
        }
        if (!Reset()) {
            return false;
        }
        std::size_t capacity = 0;
        input_ = backend_.Input(capacity);
        if (!input_ || capacity < bytes) {
            return false;
        }
        input_bytes_ = bytes;
        input_crc_ = crc;
        input_receiving_ = true;
        return true;
    }

    bool InputChunk(
        std::uint32_t offset,
        const std::uint8_t *data,
        std::size_t bytes
    )
    {
        if (!input_receiving_ || staging_.InUse() || !data || !bytes || bytes > 512 || offset != input_offset_
            || bytes > input_bytes_ - input_offset_) {
            input_receiving_ = false;
            input_ready_ = false;
            return false;
        }
        std::memcpy(input_ + offset, data, bytes);
        input_offset_ += bytes;
        return true;
    }

    bool InputCommit()
    {
        input_ready_ = input_receiving_ && input_offset_ == input_bytes_ && Crc32(input_, input_bytes_) == input_crc_;
        input_receiving_ = false;
        return input_ready_;
    }

    bool InputAdopt(
        std::uint32_t bytes,
        std::uint32_t crc
    )
    {
        if (!InputBegin(bytes, crc) || !backend_.AdoptInput(bytes)) {
            InputCancel();
            return false;
        }
        input_offset_ = bytes;
        return InputCommit();
    }

    const std::uint8_t *Result(std::size_t &bytes) const
    {
        bytes = state_ == State::kDone ? result_bytes_ : 0;
        return bytes ? result_ : nullptr;
    }
    bool InputReceiving() const { return input_receiving_; }
    void InputCancel()
    {
        input_ready_ = false;
        input_receiving_ = false;
        input_offset_ = 0;
    }

    State Status() const { return state_; }
    std::uint32_t Crc() const { return crc_; }
    std::uint32_t Elapsed() const { return elapsed_; }

    const char *Name() const
    {
        switch (state_) {
        case State::kRunning:
            return "running";
        case State::kDone:
            return "done";
        case State::kError:
            return "faulted";
        default:
            return staging_.Ready() ? "ready" : "waiting";
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
            result_ = nullptr;
            result_bytes_ = 0;
        }
    }

    Staging &staging_;
    Backend &backend_;
    State state_ = State::kIdle;
    std::uint32_t start_ = 0, elapsed_ = 0, crc_ = 0;
    std::uint8_t *input_ = nullptr;
    const std::uint8_t *result_ = nullptr;
    std::size_t input_offset_ = 0, result_bytes_ = 0;
    std::uint32_t input_bytes_ = 0, input_crc_ = 0;
    bool input_ready_ = false, input_receiving_ = false;
};

}