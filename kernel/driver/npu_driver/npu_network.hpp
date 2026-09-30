#ifndef UAI_AI_NPU_NETWORK_HPP
#define UAI_AI_NPU_NETWORK_HPP

#include <cstdint>

#include "stai.h"

namespace uai::ai::npu {

/* Optional generated-runtime trace hook used by NPU diagnostics. */
using EpochTraceCallback = void (*)(
    void *context, std::uint32_t callback_type, std::uint32_t epoch_index,
    std::uint32_t epoch_flags, std::uintptr_t epoch_address);

/*
 * Thin adapter between NpuDriver and one generated STAI network.
 *
 * This interface contains only generated-network operations. Model metadata,
 * preprocessing, decoding, and application results belong to the model and
 * its Future, not to this adapter.
 */
class NpuNetwork {
public:
    virtual stai_return_code Initialize() = 0;
    virtual stai_return_code Shutdown() = 0;
    virtual stai_return_code GetInfo(stai_network_info *info) = 0;
    virtual stai_return_code SetInput(stai_ptr input, stai_size size) = 0;
    virtual stai_return_code GetOutputs(stai_ptr *outputs,
                                        stai_size *count) = 0;
    virtual stai_return_code SetOutputs(const stai_ptr *outputs,
                                        stai_size count) = 0;
    virtual stai_return_code Run(stai_run_mode mode) = 0;
    virtual stai_return_code ContinueRun() = 0;
    virtual stai_return_code GetRunStatus() = 0;
    virtual stai_return_code NewInference() = 0;

    /* Optional diagnostics hook; inference does not depend on tracing. */
    virtual stai_return_code SetEpochTraceCallback(EpochTraceCallback callback,
                                                   void *context) = 0;

protected:
    virtual ~NpuNetwork() = default;
};

} // namespace uai::ai::npu

#endif
