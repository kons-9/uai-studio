#include "npu_multi.hpp"
#include "model_expected.hpp"
#include "model_registry.hpp"
extern "C" {
#include "stm32n6xx_hal.h"
bool experiment_npu_prepare(void);
bool experiment_npu_stop(void);
void experiment_npu_registered(void);
void experiment_npu_completed(void);
}

#if PIPE2_BUFFER_PSRAM
#error "multi-model input overlaps the fixed PSRAM camera profile; use SRAM camera buffers"
#endif

namespace {
const model_loader::ModelApi *selected = nullptr;
auto *const input = reinterpret_cast<std::uint8_t *>(0x91000000);
auto *const output = reinterpret_cast<std::uint8_t *>(0x90c00000);
}

extern "C" bool experiment_multi_deinit(void)
{
    return !selected || selected->deinit();
}

namespace experiment::model {

bool MultiBackend::Start(const Manifest &manifest)
{
    if (!manifest.tag || !manifest.output_count
        || manifest.output_count > kMaxOutputs || manifest.input_bytes > 0x200000 || manifest.output_bytes > 0x400000) {
        return false;
    }
    static_assert(model_loader::registry_count == catalog_count, "model registry and catalog disagree");
    selected = nullptr;
    for (unsigned index = 0; index < catalog_count; ++index) {
        Manifest expected{};
        if (Decode(catalog_headers[index], catalog_sizes[index], expected) && Matches(manifest, expected)) {
            selected = &model_loader::registry[index];
            break;
        }
    }
    if (!selected) {
        return false;
    }
    active_ = manifest;
    done_ = false;
    if (!experiment_npu_prepare()) {
        return false;
    }
    std::memset(output, 0, manifest.output_bytes);
    SCB_CleanDCache_by_Addr(input, (manifest.input_bytes + 31U) & ~31U);
    SCB_CleanInvalidateDCache_by_Addr(output, (manifest.output_bytes + 31U) & ~31U);
    __DSB();
    std::uint8_t *outputs[kMaxOutputs]{};
    for (unsigned index = 0; index < manifest.output_count; ++index) {
        if (std::uint64_t(manifest.outputs[index].offset) + manifest.outputs[index].bytes > manifest.output_bytes) {
            return false;
        }
        outputs[index] = output + manifest.outputs[index].offset;
    }
    experiment_npu_registered();
    HAL_NVIC_EnableIRQ(NPU0_IRQn);
    return selected->start(input, manifest.input_bytes, outputs, manifest.output_count);
}

Progress MultiBackend::Poll()
{
    if (!selected) {
        return Progress::kError;
    }
    const auto status = selected->poll();
    if (status == 1) {
        done_ = true;
        experiment_npu_completed();
        return Progress::kDone;
    }
    return status == 0 ? Progress::kRunning : Progress::kError;
}

const std::uint8_t *MultiBackend::Output(std::size_t &bytes)
{
    bytes = done_ ? active_.output_bytes : 0;
    if (!done_) {
        return nullptr;
    }
    SCB_InvalidateDCache_by_Addr(output, (bytes + 31U) & ~31U);
    __DSB();
    return output;
}

bool MultiBackend::Stop() { return experiment_npu_stop(); }
std::uint8_t *MultiBackend::Input(std::size_t &bytes) { bytes = 0x200000; return input; }
bool MultiBackend::AdoptInput(std::size_t bytes)
{
    if (bytes > 0x200000) {
        return false;
    }
    SCB_InvalidateDCache_by_Addr(input, (bytes + 31U) & ~31U);
    __DSB();
    return true;
}

}