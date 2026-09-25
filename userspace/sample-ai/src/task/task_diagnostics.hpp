#pragma once

#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_manager/memory_manager.hpp"

namespace uai::ai::task {

using uai::ai::common::Error;
using NpuStatus = uai::ai::npu_driver::Status;

void LogStatus(const char *component, const Error &error);
void LogFrameBrightness(
    const uai::ai::memory_manager::CaptureFrame &capture);
void LogNpuStatus(const NpuStatus &status);
void DumpCoreRegisters(const char *stage);
void DumpPeripheralRegisters(const char *stage);
void DumpFrozenCapture(
    const uai::ai::memory_manager::CaptureFrame &frame);

} // namespace uai::ai::task
