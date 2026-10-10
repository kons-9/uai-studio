#pragma once
#include "driver/driver_ownership.hpp"
#include "driver/console_driver/registers/console_registers.hpp"

namespace uai::ai::console {

struct Input {
    char value = 0;
    bool error = false;
};
struct Notifier {
    void *context = nullptr;
    void (*wake)(void *) = nullptr;
};
class ConsoleManagement;
class ConsoleDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;
    common::Error Read(
        Input *input,
        const Writer &writer
    );
    common::Error Write(
        const char *text,
        std::size_t size,
        const Writer &writer
    );

private:
    friend class ConsoleManagement;
    explicit ConsoleDriver(driver::ResourceManagement &management) : management_(&management) {}
    common::Error Initialize(
        Notifier notifier,
        const Writer &writer
    );
    driver::ResourceManagement *management_;
    registers::ConsoleRegisterLayer registers_{};
    bool initialized_ = false;
};
class ConsoleManagement final {
public:
    using Accessor = driver::ResourceAccessor<ConsoleDriver>;
    static ConsoleManagement &Instance()
    {
        static ConsoleManagement management;
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
        ConsoleDriver::Writer writer;
        auto status = ownership_.Acquire(&writer, timeout);
        if (status.Ok())
            *accessor = Accessor(driver_, static_cast<ConsoleDriver::Writer &&>(writer));
        return status;
    }
    common::Error Initialize(Notifier notifier = {})
    {
        auto status = ownership_.Initialize();
        if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized)
            return status;
        Accessor accessor;
        status = Acquire(&accessor);
        return status.Ok() ? accessor->Initialize(notifier, accessor.Ownership()) : status;
    }
    common::Error Read(Input *input)
    {
        Accessor accessor;
        auto status = Acquire(&accessor);
        return status.Ok() ? accessor->Read(input, accessor.Ownership()) : status;
    }
    common::Error Write(
        const char *text,
        std::size_t size
    )
    {
        Accessor accessor;
        auto status = Acquire(&accessor);
        return status.Ok() ? accessor->Write(text, size, accessor.Ownership()) : status;
    }
    ConsoleManagement(const ConsoleManagement &) = delete;
    ConsoleManagement &operator=(const ConsoleManagement &) = delete;

private:
    ConsoleManagement() : driver_(ownership_) {}
    driver::ResourceManagement ownership_{};
    ConsoleDriver driver_;
};

}