#pragma once

#include <utility>

#include "driver/nor_driver/registers/nor_registers.hpp"
#include "driver/driver_ownership.hpp"

namespace uai::ai::nor {

class NorManagement;

/* Application-facing C++ driver for the STM32N6570-DK model NOR.
 * The board-specific HAL work remains in the STM32 BSP C implementation. */
class NorDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    NorDriver(const NorDriver &) = delete;
    NorDriver &operator=(const NorDriver &) = delete;

    /* Called only while the corresponding management Accessor owns the Writer. */
    void KeepClocksOnSleep(const Writer &writer) const;

private:
    friend class NorManagement;
    NorDriver() = default;
    ~NorDriver() = default;

    /* Initialize the NOR, verify the model-data aperture, and enable mapping.
     * Call after PSRAM initialization because BSP_XSPI_RAM_Init resets XSPIM. */
    int Initialize(const Writer &writer);

    registers::NorRegisterLayer registers_{};
    bool initialized_ = false;
};

/* The management owns the single NOR driver and grants exclusive access to it.
 * Initialize() must run after the kernel starts and after PSRAM initialization. */
class NorManagement final {
public:
    class Accessor final {
    public:
        Accessor() = default;
        ~Accessor() = default;

        Accessor(const Accessor &) = delete;
        Accessor &operator=(const Accessor &) = delete;
        Accessor(Accessor &&) noexcept = default;
        Accessor &operator=(Accessor &&) noexcept = default;

        bool Valid() const { return driver_ != nullptr && writer_.Valid(); }
        void KeepClocksOnSleep() const
        {
            if (Valid()) driver_->KeepClocksOnSleep(writer_);
        }

    private:
        friend class NorManagement;
        Accessor(NorDriver &driver, NorDriver::Writer &&writer)
            : driver_(&driver), writer_(std::move(writer))
        {
        }

        NorDriver *driver_ = nullptr;
        NorDriver::Writer writer_{};
    };

    static NorManagement &Instance()
    {
        static NorManagement management;
        return management;
    }

    int Initialize()
    {
        const common::Error status = ownership_.Initialize();
        if (!status.Ok() &&
            status.Code() != common::ErrorCode::kAlreadyInitialized) {
            return -1;
        }
        NorDriver::Writer writer;
        if (!ownership_.Acquire(&writer).Ok()) return -1;
        return driver_.Initialize(writer);
    }

    common::Error Acquire(Accessor *accessor, TMO timeout = TMO_FEVR)
    {
        if (accessor == nullptr) {
            return {common::ErrorCode::kInvalidArgument};
        }
        *accessor = {};
        NorDriver::Writer writer;
        const common::Error status = ownership_.Acquire(&writer, timeout);
        if (status.Ok()) {
            *accessor = Accessor(driver_, std::move(writer));
        }
        return status;
    }

    common::Error Validate(const NorDriver::Writer &writer) const
    {
        return ownership_.Validate(writer);
    }

    NorManagement(const NorManagement &) = delete;
    NorManagement &operator=(const NorManagement &) = delete;

private:
    NorManagement() = default;
    ~NorManagement() = default;

    driver::ResourceManagement ownership_{};
    NorDriver driver_{};
};

} // namespace uai::ai::nor
