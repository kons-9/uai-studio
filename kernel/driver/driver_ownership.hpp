#ifndef UAI_AI_DRIVER_OWNERSHIP_HPP
#define UAI_AI_DRIVER_OWNERSHIP_HPP

#include <cstdint>

#include <tk/tkernel.h>

#include "middleware/foundation/error.hpp"

namespace uai::ai::driver {

/*
 * A driver owns one hardware state machine.  A Writer is the transferable
 * capability for changing that state machine; copying it is intentionally
 * impossible.  The capability is released when it leaves scope.
 *
 * The mutex is created lazily from the driver's Initialize() path because
 * driver objects live in static application storage while the kernel objects
 * must be created after usermain() starts.
 */
class ResourceManagement final {
public:
    class Writer final {
    public:
        using ReleaseFunction = void (*)(const void *, ID) noexcept;

        Writer() = default;
        Writer(const ResourceManagement *owner, ID mutex_id,
               ReleaseFunction release)
            : owner_(owner), mutex_id_(mutex_id), release_(release)
        {
        }
        ~Writer();

        Writer(const Writer &) = delete;
        Writer &operator=(const Writer &) = delete;

        Writer(Writer &&other) noexcept;
        Writer &operator=(Writer &&other) noexcept;

        bool Valid() const
        {
            return owner_ != nullptr && release_ != nullptr;
        }
        bool Matches(const ResourceManagement &owner, ID mutex_id) const
        {
            return owner_ == &owner && mutex_id_ == mutex_id;
        }

    private:
        void Release() noexcept;

        const ResourceManagement *owner_ = nullptr;
        ID mutex_id_ = 0;
        ReleaseFunction release_ = nullptr;
    };

    ResourceManagement() = default;
    ~ResourceManagement() = default;

    ResourceManagement(const ResourceManagement &) = delete;
    ResourceManagement &operator=(const ResourceManagement &) = delete;

    common::Error Initialize(const char *name);
    common::Error Acquire(Writer *writer, TMO timeout = TMO_FEVR) const;
    common::Error Validate(const Writer &writer,
                           const char *operation) const;

private:
    static void ReleaseWriter(const void *owner, ID mutex_id) noexcept;
    void Release(ID mutex_id) const noexcept;

    ID mutex_id_ = 0;
    bool initialized_ = false;
};

/* A scoped handle to a managed driver. Moving the handle transfers its Writer;
 * destroying it releases the management lock. */
template <typename Resource>
class ResourceAccessor final {
public:
    using Writer = ResourceManagement::Writer;

    ResourceAccessor() = default;
    ResourceAccessor(Resource &resource, Writer &&writer)
        : resource_(&resource), writer_(static_cast<Writer &&>(writer))
    {
    }

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

} // namespace uai::ai::driver

#endif // UAI_AI_DRIVER_OWNERSHIP_HPP
