#pragma once

#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_manager/memory_manager.hpp"

namespace uai::sample2::task {

using uai::sample2::common::Error;
using NpuStatus = uai::sample2::npu_driver::Status;

void LogStatus(const char *component, const Error &error);
void LogFrameBrightness(
    const uai::sample2::memory_manager::CaptureFrame &capture);
void LogNpuStatus(const NpuStatus &status);
void DumpCoreRegisters(const char *stage);
void DumpPeripheralRegisters(const char *stage);
void DumpFrozenCapture(
    const uai::sample2::memory_manager::CaptureFrame &frame);

} // namespace uai::sample2::task
