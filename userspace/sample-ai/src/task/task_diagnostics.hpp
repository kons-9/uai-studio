#pragma once

#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"

namespace uai::ai::task {

using uai::ai::common::Error;
using NpuStatus = uai::ai::npu::Status;

void LogStatus(const char *component, const Error &error);
void LogFrameBrightness(
    const uai::ai::memory_allocator::CaptureFrame &capture);
void LogNpuStatus(const NpuStatus &status);
void DumpCoreRegisters(const char *stage);
void DumpPeripheralRegisters(const char *stage);
void DumpFrozenCapture(
    const uai::ai::memory_allocator::CaptureFrame &frame);

} // namespace uai::ai::task
