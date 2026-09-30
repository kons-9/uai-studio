#pragma once

#include <cstdint>

#include "common/error.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "application/pipeline/frame_types.hpp"
#include "middleware/memory/buffer_types.hpp"

namespace uai::ai::task {

void LogStatus(const char *component, const common::Error &error);
void LogFrameBrightness(
    const uai::ai::pipeline::CaptureFrame &capture);
void LogInferenceInput(
    const uai::ai::pipeline::InferenceFrame &frame);
void LogNpuStatus(const uai::ai::npu::Status &status);
void DumpCoreRegisters(const char *stage);
void DumpPeripheralRegisters(const char *stage);
void DumpFrozenCapture(
    const uai::ai::pipeline::CaptureFrame &frame);

} // namespace uai::ai::task
