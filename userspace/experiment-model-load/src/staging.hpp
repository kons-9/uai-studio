#pragma once
#include "manifest.hpp"
#include <cstring>

namespace experiment::model {

class Staging {
public:
    struct Slot {
        std::uint32_t address, bytes;
        std::uint8_t *data;
    };
    Staging(
        Slot weights,
        Slot blob,
        Manifest expected
    )
        : weights_(weights),
          blob_(blob),
          expected_(expected)
    {}

    bool Begin(
        const std::uint8_t *header,
        std::size_t size
    )
    {
        if (in_use_) {
            return false;
        }
        Cancel();
        Manifest candidate{};
        if (!Decode(header, size, candidate) || !Matches(candidate, expected_) || !Accept(weights_, candidate.weights)
            || !Accept(blob_, candidate.blob) || Overlap(weights_, blob_)) {
            return false;
        }
        pending_ = candidate;
        receiving_ = true;
        return true;
    }

    bool Chunk(
        bool is_blob,
        std::uint32_t offset,
        const std::uint8_t *data,
        std::size_t size
    )
    {
        if (!receiving_ || !data || size == 0 || size > 512) {
            return false;
        }
        auto &position = is_blob ? blob_offset_ : weights_offset_;
        const auto segment = is_blob ? pending_.blob : pending_.weights;
        const auto slot = is_blob ? blob_ : weights_;
        if (offset != position || size > segment.bytes - position) {
            Cancel();
            return false;
        }
        std::memcpy(slot.data + offset, data, size);
        position += static_cast<std::uint32_t>(size);
        return true;
    }

    bool Complete()
    {
        if (!receiving_ || weights_offset_ != pending_.weights.bytes || blob_offset_ != pending_.blob.bytes
            || !Verify(weights_.data, weights_offset_, pending_.weights)
            || !Verify(blob_.data, blob_offset_, pending_.blob)) {
            Cancel();
            return false;
        }
        if (publish_ && !publish_(context_, weights_.data, weights_offset_, blob_.data, blob_offset_)) {
            Cancel();
            return false;
        }
        ready_ = true;
        receiving_ = false;
        return true;
    }

    bool Cancel()
    {
        if (in_use_) {
            return false;
        }
        receiving_ = false;
        ready_ = false;
        weights_offset_ = 0;
        blob_offset_ = 0;
        pending_ = {};
        return true;
    }
    bool Ready() const { return ready_; }
    bool Receiving() const { return receiving_; }
    bool InUse() const { return in_use_; }
    bool Reverify()
    {
        if (!ready_ || in_use_) {
            return false;
        }
        if (!Verify(weights_.data, pending_.weights.bytes, pending_.weights)
            || !Verify(blob_.data, pending_.blob.bytes, pending_.blob)) {
            Cancel();
            return false;
        }
        return true;
    }
    bool Acquire()
    {
        if (!ready_ || in_use_) {
            return false;
        }
        in_use_ = true;
        return true;
    }
    void Release() { in_use_ = false; }
    std::uint32_t Received(bool is_blob) const { return is_blob ? blob_offset_ : weights_offset_; }
    const Manifest *Verified() const { return ready_ ? &pending_ : nullptr; }
    void BeforePublish(
        void *context,
        bool (*publish)(
            void *,
            const std::uint8_t *,
            std::size_t,
            const std::uint8_t *,
            std::size_t
        )
    )
    {
        context_ = context;
        publish_ = publish;
    }

private:
    static bool Accept(
        Slot slot,
        Segment segment
    )
    {
        return slot.data && slot.bytes >= segment.bytes && slot.address == segment.address
            && Within(segment, slot.address, slot.bytes);
    }
    static bool Overlap(
        Slot first,
        Slot second
    )
    {
        const auto left = reinterpret_cast<std::uintptr_t>(first.data),
                   right = reinterpret_cast<std::uintptr_t>(second.data);
        return left <= right ? right - left < first.bytes : left - right < second.bytes;
    }
    Slot weights_, blob_;
    Manifest expected_{}, pending_{};
    std::uint32_t weights_offset_ = 0, blob_offset_ = 0;
    bool receiving_ = false, ready_ = false, in_use_ = false;
    void *context_ = nullptr;
    bool (*publish_)(
        void *,
        const std::uint8_t *,
        std::size_t,
        const std::uint8_t *,
        std::size_t
    ) = nullptr;
};

}