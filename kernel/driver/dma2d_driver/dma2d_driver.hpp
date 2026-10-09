#pragma once

#include <limits>
#include <utility>

#include "driver/driver_ownership.hpp"
#include "middleware/image_processing/operations.hpp"

namespace uai::ai::dma2d {

class Dma2dManagement;

class Dma2dDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;
    using Request = image_processing::Request;

    Dma2dDriver(const Dma2dDriver &) = delete;
    Dma2dDriver &operator=(const Dma2dDriver &) = delete;

    static bool ValidateRequest(const Request &request, std::uint32_t timeout_ms)
    {
        using namespace image_processing;
        if (!Validate(request) || request.operation == Operation::kResize || timeout_ms == 0 || timeout_ms > 1000
            || request.destination.width > 0x3fff || request.destination.height > 0xffff
            || request.destination.stride / PixelBytes(request.destination.format) - request.destination.width > 0x3fff) {
            return false;
        }
        const auto accessible = [](const Image &image) {
            const auto address = reinterpret_cast<std::uintptr_t>(image.data);
            return address % 32 == 0 && image.bytes % 32 == 0
                && image.bytes <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
                && image.bytes <= std::numeric_limits<std::uint32_t>::max()
                && address <= std::numeric_limits<std::uint32_t>::max() - image.bytes;
        };
        return accessible(request.destination)
            && (request.operation == Operation::kFill || accessible(request.source))
            && (request.operation != Operation::kBlend || accessible(request.background));
    }

    common::Error Transfer(const Request &request, std::uint32_t timeout_ms, const Writer &writer);
    void KeepClocksOnSleep(const Writer &writer) const;

private:
    friend class Dma2dManagement;
    Dma2dDriver() = default;
    ~Dma2dDriver() = default;
    common::Error Initialize(const Writer &writer);
    bool initialized_ = false;
};

class Dma2dManagement final {
public:
    using Writer = Dma2dDriver::Writer;
    using Accessor = driver::ResourceAccessor<Dma2dDriver>;

    static Dma2dManagement &Instance()
    {
        static Dma2dManagement management;
        return management;
    }

    common::Error Initialize()
    {
        common::Error status = ownership_.Initialize();
        if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized) {
            return status;
        }
        Accessor accessor;
        status = Acquire(&accessor);
        return status.Ok() ? driver_.Initialize(accessor.Ownership()) : status;
    }

    common::Error Acquire(Accessor *accessor, TMO timeout = TMO_FEVR)
    {
        if (accessor == nullptr) {
            return {common::ErrorCode::kInvalidArgument};
        }
        *accessor = {};
        Writer writer;
        const common::Error status = ownership_.Acquire(&writer, timeout);
        if (status.Ok()) {
            *accessor = Accessor(driver_, std::move(writer));
        }
        return status;
    }

    common::Error Validate(const Writer &writer) const { return ownership_.Validate(writer); }

    common::Error Transfer(const Dma2dDriver::Request &request, std::uint32_t timeout_ms = 100)
    {
        if (!Dma2dDriver::ValidateRequest(request, timeout_ms)) {
            return {common::ErrorCode::kInvalidArgument};
        }
        Accessor accessor;
        const common::Error status = Acquire(&accessor, static_cast<TMO>(timeout_ms));
        return status.Ok() ? accessor.Get()->Transfer(request, timeout_ms, accessor.Ownership()) : status;
    }

    bool Run(const Dma2dDriver::Request &request, std::uint32_t timeout_ms)
    {
        return Transfer(request, timeout_ms).Ok();
    }

    void KeepClocksOnSleep()
    {
        Accessor accessor;
        if (Acquire(&accessor).Ok()) {
            accessor->KeepClocksOnSleep(accessor.Ownership());
        }
    }

    Dma2dManagement(const Dma2dManagement &) = delete;
    Dma2dManagement &operator=(const Dma2dManagement &) = delete;

private:
    Dma2dManagement() = default;
    ~Dma2dManagement() = default;
    driver::ResourceManagement ownership_{};
    Dma2dDriver driver_{};
};

}