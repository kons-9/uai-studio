#include "models/person/future.hpp"

#include <cstdint>

#include "common/log.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"

namespace uai::ai::models::person {

void Future::Reset(const FutureContext &context,
                   const memory_allocator::InferenceFrame &frame)
{
    context_ = context;
    frame_ = frame;
    phase_ = Phase::kPreprocess;
    preprocess_stage_logged_ = false;
    infer_stage_logged_ = false;
    postprocess_stage_logged_ = false;
}

bool Future::TryClaim()
{
    bool expected = false;
    return occupied_.compare_exchange_strong(expected, true);
}

void Future::ReleaseClaim()
{
    occupied_.store(false, std::memory_order_release);
}

ai_runtime::AiModelId Future::model_id() const
{
    return static_cast<ai_runtime::AiModelId>(ModelKind::kPerson);
}

std::uint32_t Future::step_id() const
{
    return static_cast<std::uint32_t>(phase_);
}

common::Error Future::Preprocess()
{
    if (context_.model == nullptr || context_.cache == nullptr ||
        context_.info == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "person.future.preprocess.context"};
    }
    if (!preprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: person preprocess begin seq=%u buffer=%x\n"),
                     static_cast<unsigned int>(frame_.capture_sequence),
                     static_cast<unsigned int>(frame_.buffer.address));
    }
    if (!frame_ || !frame_.from_pipe2 ||
        frame_.output_count < context_.info->n_outputs ||
        frame_.buffer.size < context_.info->inputs[0].size_bytes) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "person.future.frame"};
    }
    common::Error status = context_.model->PrepareInput(frame_, *context_.cache);
    if (!status.Ok()) return status;
    const memory_allocator::Buffer &input =
        frame_.source_valid ? frame_.source : frame_.buffer;
    if (!input || input.size < context_.info->inputs[0].size_bytes) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "person.future.input"};
    }

    /* Pipe2 wrote this buffer using DMA. A CPU read/invalidate here must not
     * overwrite the DMA image with dirty cache lines. */
    const memory_allocator::Buffer range{
        input.address,
        context_.info->inputs[0].size_bytes,
        input.index,
        memory_allocator::Region::kInference};
    status = frame_.source_valid
                 ? context_.cache->PrepareForPeripheralRead(range)
                 : context_.cache->PrepareForCpuRead(range);
    if (status.Ok() && !preprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: person preprocess done seq=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence));
        preprocess_stage_logged_ = true;
    }
    return status;
}

common::Error Future::Infer()
{
    if (context_.model == nullptr || context_.npu == nullptr ||
        context_.info == nullptr) {
        return {common::ErrorCode::kNotInitialized,
                0U,
                "person.future.infer.context"};
    }
    if (!infer_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: person infer begin seq=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence));
    }
    const memory_allocator::Buffer &input =
        frame_.source_valid ? frame_.source : frame_.buffer;
    npu::Status result = context_.npu->SetInput(
        reinterpret_cast<stai_ptr>(input.address),
        context_.info->inputs[0].size_bytes);
    if (!result.Ok()) return result.error;

    stai_ptr outputs[kMaxModelOutputs]{};
    for (std::uint16_t i = 0U; i < context_.info->n_outputs; ++i) {
        const auto &output = frame_.outputs[i];
        if (!output || output.size < context_.info->outputs[i].size_bytes ||
            output.alignment == 0U ||
            output.address % output.alignment != 0U) {
            return {common::ErrorCode::kInvalidArgument, i,
                    "person.future.output_buffer"};
        }
        outputs[i] = reinterpret_cast<stai_ptr>(output.address);
    }
    result = context_.npu->SetOutputs(outputs, context_.info->n_outputs);
    if (!result.Ok()) return result.error;
    result = context_.npu->Run();
    if (!result.Ok()) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: person infer failed code=%u detail=%u op=%s\n"),
                     static_cast<unsigned int>(result.error.code),
                     static_cast<unsigned int>(result.error.detail),
                     result.error.operation);
        return result.error;
    }
    if (!infer_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: person infer done seq=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence));
        infer_stage_logged_ = true;
    }
    result = context_.npu->NewInference();
    return result.error;
}

common::Error Future::Postprocess()
{
    if (context_.model == nullptr || context_.cache == nullptr ||
        context_.info == nullptr || context_.publish == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "person.future.postprocess.context"};
    }
    ModelOutputView view{};
    view.count = context_.info->n_outputs;
    for (std::uint16_t i = 0U; i < context_.info->n_outputs; ++i) {
        const auto &output = frame_.outputs[i];
        const memory_allocator::Buffer range{
            output.address,
            context_.info->outputs[i].size_bytes,
            output.index,
            memory_allocator::Region::kInference};
        common::Error status = context_.cache->PrepareForCpuRead(range);
        if (!status.Ok()) return status;
        view.tensors[i] = {
            reinterpret_cast<const void *>(output.address),
            {context_.info->outputs[i].size_bytes,
             context_.info->outputs[i].scale.data[0],
             context_.info->outputs[i].zeropoint.data[0]}};
    }

    const auto &descriptor = context_.model->GetDescriptor();
    InferenceGeometry geometry{};
    geometry.projection = InputProjection::kLetterboxed;
    geometry.frame_width = memory_allocator::kConfig.frame_width;
    geometry.frame_height = memory_allocator::kConfig.frame_height;
    geometry.model_width = descriptor.input_width;
    geometry.model_height = descriptor.input_height;
    geometry.content_height =
        (descriptor.input_width * memory_allocator::kConfig.inference_source_height +
         memory_allocator::kConfig.inference_source_width - 1U) /
        memory_allocator::kConfig.inference_source_width;
    geometry.pad_top = (geometry.model_height - geometry.content_height) / 2U;

    const ModelCallbacks callbacks = context_.model->GetCallbacks();
    if (callbacks.on_inference_complete == nullptr ||
        callbacks.user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "person.future.decoder_callbacks"};
    }
    const InferenceCompletionContext decode_context{view, geometry};
    ModelResult decoded{};
    common::Error status = callbacks.on_inference_complete(
        decode_context, &decoded, callbacks.user_data);
    if (!status.Ok()) return status;

    memory_allocator::BoxSet boxes{};
    status = context_.model->ConvertResult(decoded, &boxes);
    if (!status.Ok()) return status;
    context_.publish(context_.publish_context, boxes);
    if (!postprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: person postprocess done seq=%u boxes=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence),
                     static_cast<unsigned int>(boxes.person.count));
        postprocess_stage_logged_ = true;
    }
    return {};
}

ai_runtime::AiRuntimeResult Future::Evaluate()
{
    common::Error status{};
    switch (phase_) {
    case Phase::kPreprocess:
        status = Preprocess();
        if (status.Ok()) {
            phase_ = Phase::kNpu;
            return {{}, {ai_runtime::ExecutionContext::kNpu}, false};
        }
        break;
    case Phase::kNpu:
        status = Infer();
        if (status.Ok()) {
            phase_ = Phase::kPostprocess;
            return {{}, {ai_runtime::ExecutionContext::kPostprocessCpu},
                    false};
        }
        break;
    case Phase::kPostprocess:
        status = Postprocess();
        return {status, {}, true};
    }
    return {status, {}, false};
}

} // namespace uai::ai::models::person
