#pragma once

#include "driver/driver_ownership.hpp"
#include "driver/lcd_driver/registers/lcd_registers.hpp"
#include "middleware/buffer/buffer_types.hpp"

namespace uai::ai::lcd {

inline constexpr std::size_t kDisplayWidth = 800U;
inline constexpr std::size_t kDisplayHeight = 480U;
inline constexpr std::size_t kDisplayBytes = kDisplayWidth * kDisplayHeight * sizeof(std::uint16_t);

class DisplayManagement;
class LcdManagement;

class DisplayDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    common::Error Present(
        const buffer::Buffer &frame,
        const Writer &writer
    );
    common::Error Synchronize(const Writer &writer);

private:
    friend class DisplayManagement;
    explicit DisplayDriver(driver::ResourceManagement &management) : management_(&management) {}
    common::Error Initialize(
        const buffer::Buffer &initial,
        const Writer &writer
    );
    driver::ResourceManagement *management_;
    registers::LcdRegisterLayer registers_{};
    bool initialized_ = false;
};

class DisplayManagement final {
public:
    using Writer = DisplayDriver::Writer;
    using Accessor = driver::ResourceAccessor<DisplayDriver>;
    static DisplayManagement &Instance()
    {
        static DisplayManagement management;
        return management;
    }
    common::Error Acquire(
        Accessor *accessor,
        TMO timeout = TMO_FEVR
    )
    {
        if (accessor == nullptr)
            return {common::ErrorCode::kInvalidArgument};
        *accessor = {};
        driver::ResourceManagement::Writer writer;
        auto status = ownership_.Acquire(&writer, timeout);
        if (status.Ok())
            *accessor = Accessor(driver_, static_cast<driver::ResourceManagement::Writer &&>(writer));
        return status;
    }
    common::Error Initialize(const buffer::Buffer &initial)
    {
        auto status = ownership_.Initialize();
        if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized)
            return status;
        Accessor accessor;
        status = Acquire(&accessor);
        return status.Ok() ? driver_.Initialize(initial, accessor.Ownership()) : status;
    }
    common::Error Initialize(
        const buffer::Buffer &initial,
        const Writer &writer
    )
    {
        return driver_.Initialize(initial, writer);
    }
    common::Error Present(
        const buffer::Buffer &frame,
        const Writer &writer
    )
    {
        return driver_.Present(frame, writer);
    }
    common::Error Synchronize(const Writer &writer) { return driver_.Synchronize(writer); }
    common::Error Present(const buffer::Buffer &frame)
    {
        Accessor accessor;
        auto status = Acquire(&accessor);
        return status.Ok() ? accessor->Present(frame, accessor.Ownership()) : status;
    }
    common::Error Synchronize()
    {
        Accessor accessor;
        auto status = Acquire(&accessor);
        return status.Ok() ? accessor->Synchronize(accessor.Ownership()) : status;
    }
    DisplayManagement(const DisplayManagement &) = delete;
    DisplayManagement &operator=(const DisplayManagement &) = delete;

private:
    friend class LcdManagement;
    DisplayManagement() : driver_(ownership_) {}
    driver::ResourceManagement ownership_{};
    DisplayDriver driver_;
};

}