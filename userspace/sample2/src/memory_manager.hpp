#ifndef MEMORY_MANAGER_HPP
#define MEMORY_MANAGER_HPP

#include <cstddef>
#include <cstdint>

namespace memory
{

    enum class Region : std::uint8_t
    {
        kInternalMedia,
        kExternalPsram,
    };

    struct Buffer
    {
        std::uintptr_t address = 0;
        std::size_t size = 0;
        Region region = Region::kInternalMedia;

        explicit operator bool() const { return address != 0 && size != 0; }
    };

    class Manager final
    {
    public:
        /* reserve_model_pools keeps the first 16 MiB of PSRAM for the generated
         * Neural-ART virtual pool used by the person model. */
        bool Initialize(bool reserve_model_pools = false);

        /* Initialize and memory-map the XSPI2 NOR that contains the model
         * weights. This is kept separate so camera-only preview can start even
         * when the AI storage is unavailable. */
        bool InitializeModelStorage();

        /* Keep the memory/peripheral clocks required by camera, display, and
         * Neural-ART active while the asynchronous runtime executes WFE. */
        void KeepInferenceClocksOnSleep() const;

        Buffer Allocate(Region region, std::size_t size,
                        std::size_t alignment = 32U);

        void PrepareForDmaWrite(const Buffer &buffer) const;
        void PrepareForCpuRead(const Buffer &buffer) const;
        void PrepareForDisplayRead(const Buffer &buffer) const;

    private:
        struct RegionState
        {
            std::uintptr_t base = 0;
            std::size_t size = 0;
            std::size_t next = 0;
        };

        RegionState internal_{};
        RegionState psram_{};
        bool initialized_ = false;
        bool model_storage_initialized_ = false;
    };

} // namespace memory

#endif /* MEMORY_MANAGER_HPP */
