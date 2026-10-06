#pragma once

#include "middleware/foundation/error.hpp"

namespace uai::ai::resource_ownership {

/*
 * Exclusive right to operate one resource. Move-only; the right is returned
 * when the token is destroyed or overwritten. The token identifies the
 * ownership object and the lock it came from, so a token from another
 * ownership (or a stale lock) never validates.
 */
class OwnershipToken final {
public:
    using Lock = int;
    using ReleaseFunction = void (*)(const void *, Lock) noexcept;

    OwnershipToken() = default;
    OwnershipToken(const void *owner, Lock lock, ReleaseFunction release)
        : owner_(owner), lock_(lock), release_(release) {}
    ~OwnershipToken() { Release(); }

    OwnershipToken(const OwnershipToken &) = delete;
    OwnershipToken &operator=(const OwnershipToken &) = delete;

    OwnershipToken(OwnershipToken &&other) noexcept
        : owner_(other.owner_), lock_(other.lock_), release_(other.release_)
    {
        other.Forget();
    }
    OwnershipToken &operator=(OwnershipToken &&other) noexcept
    {
        if (this != &other) {
            Release();
            owner_ = other.owner_;
            lock_ = other.lock_;
            release_ = other.release_;
            other.Forget();
        }
        return *this;
    }

    bool Valid() const { return owner_ != nullptr && release_ != nullptr; }
    bool Matches(const void *owner, Lock lock) const
    {
        return owner_ == owner && lock_ == lock;
    }

private:
    void Forget()
    {
        owner_ = nullptr;
        lock_ = 0;
        release_ = nullptr;
    }
    void Release() noexcept
    {
        if (release_ != nullptr) {
            release_(owner_, lock_);
            Forget();
        }
    }

    const void *owner_ = nullptr;
    Lock lock_ = 0;
    ReleaseFunction release_ = nullptr;
};

/*
 * Hands out one OwnershipToken at a time, backed by Backend's lock.
 * Backend provides:
 *   using Timeout = ...;            // platform wait specifier
 *   static constexpr Timeout kForever;
 *   common::Error Create(Lock *lock);     // kAlreadyInitialized handled here
 *   common::Error Lock(Lock lock, Timeout timeout);   // kTimeout / kOwnership
 *   void Unlock(Lock lock) noexcept;
 * Initialize() runs after the OS is up because the backend creates a kernel
 * object. The ownership object must outlive every token it issued.
 */
template <typename Backend>
class ResourceOwnership final {
public:
    using Writer = OwnershipToken;
    using Timeout = typename Backend::Timeout;

    ResourceOwnership() = default;
    ResourceOwnership(const ResourceOwnership &) = delete;
    ResourceOwnership &operator=(const ResourceOwnership &) = delete;

    common::Error Initialize()
    {
        if (initialized_) return {common::ErrorCode::kAlreadyInitialized};
        OwnershipToken::Lock lock = 0;
        const common::Error status = backend_.Create(&lock);
        if (!status.Ok()) return status;
        lock_ = lock;
        initialized_ = true;
        return {};
    }

    common::Error Acquire(Writer *writer,
                          Timeout timeout = Backend::kForever) const
    {
        if (writer == nullptr) return {common::ErrorCode::kInvalidArgument};
        *writer = {};
        if (!initialized_) return {common::ErrorCode::kNotInitialized};
        const common::Error status = backend_.Lock(lock_, timeout);
        if (!status.Ok()) return status;
        *writer = Writer(this, lock_, &ResourceOwnership::ReleaseToken);
        return {};
    }

    common::Error Validate(const Writer &writer) const
    {
        if (!initialized_) return {common::ErrorCode::kNotInitialized};
        if (!writer.Matches(this, lock_)) return {common::ErrorCode::kOwnership};
        return {};
    }

private:
    static void ReleaseToken(const void *owner, OwnershipToken::Lock lock) noexcept
    {
        if (owner == nullptr) return;
        const auto &self = *static_cast<const ResourceOwnership *>(owner);
        if (self.initialized_ && lock == self.lock_) self.backend_.Unlock(lock);
    }

    mutable Backend backend_{};
    OwnershipToken::Lock lock_ = 0;
    bool initialized_ = false;
};

/* A scoped handle to a managed resource. Moving the handle transfers its
 * token; destroying it releases the lock. */
template <typename Resource>
class ResourceAccessor final {
public:
    using Writer = OwnershipToken;

    ResourceAccessor() = default;
    ResourceAccessor(Resource &resource, Writer &&writer)
        : resource_(&resource), writer_(static_cast<Writer &&>(writer)) {}

    ResourceAccessor(const ResourceAccessor &) = delete;
    ResourceAccessor &operator=(const ResourceAccessor &) = delete;
    ResourceAccessor(ResourceAccessor &&) noexcept = default;
    ResourceAccessor &operator=(ResourceAccessor &&) noexcept = default;

    bool Valid() const { return resource_ != nullptr && writer_.Valid(); }
    Resource *Get() const { return Valid() ? resource_ : nullptr; }
    Resource *operator->() const { return Get(); }
    const Writer &Ownership() const { return writer_; }

private:
    Resource *resource_ = nullptr;
    Writer writer_{};
};

} // namespace uai::ai::resource_ownership
