#pragma once

#include "middleware/foundation/error.hpp"
#include "driver/driver_ownership.hpp"
#include "middleware/buffer/buffer_types.hpp"

namespace uai::ai::cache {

class CacheManagement;

/* Owns CACHEAXI startup and cache maintenance for DMA/NPU memory transfers. */
class CacheDriver final {
public:
    using Writer = driver::ResourceManagement::Writer;

    CacheDriver(const CacheDriver &) = delete;
    CacheDriver &operator=(const CacheDriver &) = delete;
    static common::Error Clean(
        void *address,
        std::size_t bytes
    );
    static common::Error CleanInvalidate(
        void *address,
        std::size_t bytes
    );
    static common::Error Invalidate(
        void *address,
        std::size_t bytes
    );
    common::Error PrepareForDmaWrite(const buffer::Buffer &buffer) const;
    common::Error PrepareForDmaWrite(
        const buffer::Buffer &buffer,
        const Writer &writer
    ) const;
    common::Error PrepareForCpuRead(const buffer::Buffer &buffer) const;
    common::Error PrepareForCpuRead(
        const buffer::Buffer &buffer,
        const Writer &writer
    ) const;
    common::Error PrepareForPeripheralRead(const buffer::Buffer &buffer) const;
    common::Error PrepareForPeripheralRead(
        const buffer::Buffer &buffer,
        const Writer &writer
    ) const;
    void KeepClocksOnSleep() const;
    void KeepClocksOnSleep(const Writer &writer) const;

private:
    friend class CacheManagement;
    CacheDriver() = default;
    ~CacheDriver() = default;
    common::Error Initialize(const Writer &writer);
    bool initialized_ = false;
};

class CacheManagement final {
public:
    using Writer = CacheDriver::Writer;
    using Accessor = driver::ResourceAccessor<CacheDriver>;
    static CacheManagement &Instance()
    {
        static CacheManagement management;
        return management;
    }
    common::Error Initialize()
    {
        common::Error status = ownership_.Initialize();
        if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized)
            return status;
        Accessor accessor;
        status = Acquire(&accessor);
        return status.Ok() ? driver_.Initialize(accessor.Ownership()) : status;
    }
    common::Error Acquire(
        Accessor *accessor,
        TMO timeout = TMO_FEVR
    )
    {
        if (accessor == nullptr)
            return {common::ErrorCode::kInvalidArgument};
        *accessor = {};
        CacheDriver::Writer writer;
        const common::Error status = ownership_.Acquire(&writer, timeout);
        if (status.Ok())
            *accessor = Accessor(driver_, static_cast<Writer &&>(writer));
        return status;
    }
    common::Error Validate(const Writer &writer) const { return ownership_.Validate(writer); }
    common::Error PrepareForDmaWrite(const buffer::Buffer &buffer)
    {
        return WithWriter([&](CacheDriver &d, const Writer &w) {
            return d.PrepareForDmaWrite(buffer, w);
        });
    }
    common::Error PrepareForCpuRead(const buffer::Buffer &buffer)
    {
        return WithWriter([&](CacheDriver &d, const Writer &w) {
            return d.PrepareForCpuRead(buffer, w);
        });
    }
    common::Error PrepareForPeripheralRead(const buffer::Buffer &buffer)
    {
        return WithWriter([&](CacheDriver &d, const Writer &w) {
            return d.PrepareForPeripheralRead(buffer, w);
        });
    }
    void KeepClocksOnSleep()
    {
        (void)WithWriter([](CacheDriver &d, const Writer &w) {
            d.KeepClocksOnSleep(w);
            return common::Error{common::ErrorCode::kOk};
        });
    }
    CacheManagement(const CacheManagement &) = delete;
    CacheManagement &operator=(const CacheManagement &) = delete;

private:
    template <typename Operation>
    common::Error WithWriter(Operation operation)
    {
        Accessor accessor;
        const common::Error status = Acquire(&accessor);
        if (!status.Ok())
            return status;
        return operation(*accessor.Get(), accessor.Ownership());
    }
    CacheManagement() = default;
    ~CacheManagement() = default;
    driver::ResourceManagement ownership_{};
    CacheDriver driver_{};
};

} // namespace uai::ai::cache
