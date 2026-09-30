#include "driver/driver_ownership.hpp"

namespace uai::ai::driver {
namespace {

common::Error MutexError(ER status, const char *operation)
{
    const common::ErrorCode code =
        status == E_TMOUT ? common::ErrorCode::kTimeout
                          : common::ErrorCode::kOwnership;
    return {code, static_cast<std::uint32_t>(status), operation};
}

} // namespace

ResourceManagement::Writer::~Writer()
{
    Release();
}

ResourceManagement::Writer::Writer(Writer &&other) noexcept
    : owner_(other.owner_), mutex_id_(other.mutex_id_), release_(other.release_)
{
    other.owner_ = nullptr;
    other.mutex_id_ = 0;
    other.release_ = nullptr;
}

ResourceManagement::Writer &ResourceManagement::Writer::operator=(Writer &&other) noexcept
{
    if (this != &other) {
        Release();
        owner_ = other.owner_;
        mutex_id_ = other.mutex_id_;
        release_ = other.release_;
        other.owner_ = nullptr;
        other.mutex_id_ = 0;
        other.release_ = nullptr;
    }
    return *this;
}

void ResourceManagement::Writer::Release() noexcept
{
    if (release_ != nullptr) {
        release_(owner_, mutex_id_);
        owner_ = nullptr;
        mutex_id_ = 0;
        release_ = nullptr;
    }
}

common::Error ResourceManagement::Initialize(const char *name)
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                name == nullptr ? "driver.management" : name};
    }

    T_CMTX mutex = {};
    mutex.mtxatr = TA_INHERIT;
    const ID created_mutex_id = tk_cre_mtx(&mutex);
    if (created_mutex_id < E_OK) {
        mutex_id_ = 0;
        return MutexError(created_mutex_id,
                          name == nullptr ? "driver.management.create"
                                           : name);
    }
    mutex_id_ = created_mutex_id;
    initialized_ = true;
    return {common::ErrorCode::kOk, static_cast<std::uint32_t>(mutex_id_),
            name == nullptr ? "driver.management" : name};
}

common::Error ResourceManagement::Acquire(Writer *writer, TMO timeout) const
{
    if (writer == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "driver.management.acquire.null_writer"};
    }
    *writer = {};
    if (!initialized_ || mutex_id_ < E_OK) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "driver.management.acquire.not_initialized"};
    }

    const ER status = tk_loc_mtx(mutex_id_, timeout);
    if (status != E_OK) {
        return MutexError(status, "driver.management.acquire");
    }
    *writer = Writer(this, mutex_id_, &ResourceManagement::ReleaseWriter);
    return {common::ErrorCode::kOk, static_cast<std::uint32_t>(mutex_id_),
            "driver.management.acquire"};
}

common::Error ResourceManagement::Validate(const Writer &writer,
                                          const char *operation) const
{
    if (!initialized_ || mutex_id_ < E_OK) {
        return {common::ErrorCode::kNotInitialized, 0U,
                operation == nullptr ? "driver.management.validate"
                                     : operation};
    }
    if (!writer.Matches(*this, mutex_id_)) {
        return {common::ErrorCode::kOwnership, 0U,
                operation == nullptr ? "driver.management.validate"
                                     : operation};
    }
    return {common::ErrorCode::kOk, static_cast<std::uint32_t>(mutex_id_),
            operation == nullptr ? "driver.management.validate" : operation};
}

void ResourceManagement::ReleaseWriter(const void *owner, ID mutex_id) noexcept
{
    if (owner != nullptr) {
        static_cast<const ResourceManagement *>(owner)->Release(mutex_id);
    }
}

void ResourceManagement::Release(ID mutex_id) const noexcept
{
    if (initialized_ && mutex_id == mutex_id_ && mutex_id_ >= E_OK) {
        (void)tk_unl_mtx(mutex_id_);
    }
}

} // namespace uai::ai::driver
