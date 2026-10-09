#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "driver/driver_ownership.hpp"

namespace uai::ai::peripheral {

std::uint32_t CycleCount();

enum class DmaController {
    kGeneralPurpose,
    kHighPerformance
};

struct Memory {
    std::uint8_t *data = nullptr;
    std::size_t size = 0;
};

struct Calendar {
    std::uint8_t year = 0;
    std::uint8_t month = 1;
    std::uint8_t day = 1;
    std::uint8_t weekday = 1;
    std::uint8_t hours = 0;
    std::uint8_t minutes = 0;
    std::uint8_t seconds = 0;
};

struct CounterSample {
    std::uint32_t ticks = 0;
    std::uint32_t cycles = 0;
    std::uint32_t frequency = 0;
    std::uint32_t cycle_frequency = 0;
    bool cycles_available = false;
};

class PeripheralManagement;

class PeripheralDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    common::Error RandomWords(
        std::uint32_t *output,
        std::size_t count,
        std::uint32_t timeout_ms,
        const Writer &writer
    );
    common::Error Sha256(
        const Memory &input,
        std::size_t bytes,
        std::uint8_t (&digest)[32],
        std::uint32_t timeout_ms,
        const Writer &writer
    );
    common::Error Crc32Mpeg2(
        const Memory &input,
        std::size_t bytes,
        std::uint32_t *value,
        const Writer &writer
    );
    common::Error Copy(
        DmaController controller,
        const Memory &source,
        const Memory &destination,
        std::size_t offset,
        std::size_t bytes,
        std::uint32_t timeout_ms,
        const Writer &writer
    );
    common::Error StartCounter(
        std::uint32_t frequency,
        const Writer &writer
    );
    common::Error ReadCounter(
        CounterSample *sample,
        const Writer &writer
    ) const;
    common::Error StopCounter(const Writer &writer);
    common::Error OpenCalendar(const Writer &writer);
    common::Error SetCalendar(
        const Calendar &calendar,
        const Writer &writer
    );
    common::Error ReadCalendar(
        Calendar *calendar,
        const Writer &writer
    ) const;

    PeripheralDriver(const PeripheralDriver &) = delete;
    PeripheralDriver &operator=(const PeripheralDriver &) = delete;

private:
    friend class PeripheralManagement;
    PeripheralDriver() = default;
    ~PeripheralDriver() = default;
    common::Error Close(const Writer &writer);
    bool counter_open_ = false;
    bool calendar_open_ = false;
    std::uint32_t saved_debug_control_ = 0;
    std::uint32_t saved_cycle_control_ = 0;
    std::uint32_t counter_frequency_ = 0;
    bool cycles_available_ = false;
};

class PeripheralManagement final {
public:
    class Accessor final {
    public:
        Accessor() = default;
        ~Accessor() { (void)Close(); }
        Accessor(const Accessor &) = delete;
        Accessor &operator=(const Accessor &) = delete;
        Accessor(Accessor &&other) noexcept
            : driver_(
                  std::exchange(
                      other.driver_,
                      nullptr
                  )
              ),
              writer_(std::move(other.writer_))
        {}
        Accessor &operator=(Accessor &&other) noexcept
        {
            if (this != &other) {
                (void)Close();
                driver_ = std::exchange(other.driver_, nullptr);
                writer_ = std::move(other.writer_);
            }
            return *this;
        }
        bool Valid() const { return driver_ != nullptr && writer_.Valid(); }
        PeripheralDriver *operator->() const { return driver_; }
        const PeripheralDriver::Writer &Ownership() const { return writer_; }
        common::Error Close()
        {
            const auto status = Valid() ? driver_->Close(writer_) : common::Error{};
            driver_ = nullptr;
            writer_ = {};
            return status;
        }

    private:
        friend class PeripheralManagement;
        Accessor(
            PeripheralDriver &driver,
            PeripheralDriver::Writer &&writer
        )
            : driver_(&driver),
              writer_(std::move(writer))
        {}
        PeripheralDriver *driver_ = nullptr;
        PeripheralDriver::Writer writer_{};
    };

    static PeripheralManagement &Instance()
    {
        static PeripheralManagement management;
        return management;
    }
    common::Error Acquire(
        Accessor *accessor,
        TMO timeout = TMO_FEVR
    )
    {
        if (accessor == nullptr) {
            return {common::ErrorCode::kInvalidArgument};
        }
        *accessor = {};
        const auto initialized = ownership_.Initialize();
        if (!initialized.Ok() && initialized.Code() != common::ErrorCode::kAlreadyInitialized) {
            return initialized;
        }
        PeripheralDriver::Writer writer;
        const auto status = ownership_.Acquire(&writer, timeout);
        if (status.Ok()) {
            *accessor = Accessor(driver_, std::move(writer));
        }
        return status;
    }
    common::Error Validate(const PeripheralDriver::Writer &writer) const { return ownership_.Validate(writer); }
    PeripheralManagement(const PeripheralManagement &) = delete;
    PeripheralManagement &operator=(const PeripheralManagement &) = delete;

private:
    PeripheralManagement() = default;
    ~PeripheralManagement() = default;
    driver::ResourceManagement ownership_{};
    PeripheralDriver driver_{};
};

}