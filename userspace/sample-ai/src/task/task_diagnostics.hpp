#pragma once

#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"

namespace uai::ai::task {

void LogStatus(const char *component, const common::Error &error);
void LogFrameBrightness(
    const uai::ai::memory_allocator::CaptureFrame &capture);
void LogInferenceInput(
    const uai::ai::memory_allocator::InferenceFrame &frame);
void LogNpuStatus(const uai::ai::npu::Status &status);
void DumpCoreRegisters(const char *stage);
void DumpPeripheralRegisters(const char *stage);
void DumpFrozenCapture(
    const uai::ai::memory_allocator::CaptureFrame &frame);

} // namespace uai::ai::task
