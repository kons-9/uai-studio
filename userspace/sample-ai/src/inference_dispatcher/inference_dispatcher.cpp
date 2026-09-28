#include "inference_dispatcher/inference_dispatcher.hpp"

#include "common/log.hpp"

#include <cstddef>
#include <cstdint>

namespace uai::ai {

namespace {

std::int16_t ClampCoordinate(float value, std::int32_t limit)
{
    if (value <= 0.0F) {
        return 0;
    }
    if (value >= static_cast<float>(limit)) {
        return static_cast<std::int16_t>(limit);
    }
    return static_cast<std::int16_t>(value);
}

models::ModelOutputSpec BuildOutputSpec(const stai_network_info &info)
{
    models::ModelOutputSpec spec{};
    spec.count = info.n_outputs;
    for (std::uint16_t i = 0U; i < info.n_outputs &&
                              i < models::kMaxModelOutputs;
         ++i) {
        spec.tensors[i].size_bytes = info.outputs[i].size_bytes;
        spec.tensors[i].scale = info.outputs[i].scale.data[0];
        spec.tensors[i].zero_point = info.outputs[i].zeropoint.data[0];
    }
    return spec;
}

models::InferenceGeometry BuildInferenceGeometry(
    const models::ModelDescriptor &descriptor, bool from_pipe2)
{
    models::InferenceGeometry geometry{};
    geometry.projection = from_pipe2
                              ? models::InputProjection::kLetterboxed
                              : models::InputProjection::kCenteredSquare;
    geometry.frame_width = memory_allocator::kFrameWidth;
    geometry.frame_height = memory_allocator::kFrameHeight;
    geometry.model_width = descriptor.input_width;
    geometry.model_height = descriptor.input_height;
    geometry.content_height =
        (descriptor.input_width * memory_allocator::kInferenceSourceHeight +
         memory_allocator::kInferenceSourceWidth - 1U) /
        memory_allocator::kInferenceSourceWidth;
    geometry.pad_top = geometry.model_height > geometry.content_height
                           ? (geometry.model_height - geometry.content_height) /
                                 2U
                           : 0U;
    return geometry;
}

void CopyDetections(const models::ModelResult &source,
                    memory_allocator::DetectionSet *destination)
{
    destination->count = source.detection_count < memory_allocator::kMaxBoxes
                             ? source.detection_count
                             : memory_allocator::kMaxBoxes;
    for (std::uint32_t i = 0U; i < destination->count; ++i) {
        const models::Detection &detection = source.detections[i];
        destination->boxes[i].x = ClampCoordinate(
            detection.x, memory_allocator::kFrameWidth);
        destination->boxes[i].y = ClampCoordinate(
            detection.y, memory_allocator::kFrameHeight);
        destination->boxes[i].width = ClampCoordinate(
            detection.width, memory_allocator::kFrameWidth);
        destination->boxes[i].height = ClampCoordinate(
            detection.height, memory_allocator::kFrameHeight);
        destination->boxes[i].confidence = detection.confidence;
    }
}

} // namespace

common::Error InferenceDispatcher::Initialize(
    npu_scheduler::NpuScheduler &scheduler,
    memory_allocator::MemoryAllocator &memory, cache::CacheDriver &cache)
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "ai.dispatcher.initialize"};
    }
    if (!scheduler.Initialized()) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.dispatcher.scheduler"};
    }
    scheduler_ = &scheduler;
    memory_ = &memory;
    cache_ = &cache;
    const common::Error status = ConfigureCurrentModel();
    if (!status.Ok()) {
        scheduler_ = nullptr;
        memory_ = nullptr;
        cache_ = nullptr;
        return status;
    }
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "ai.dispatcher.initialize"};
}

common::Error InferenceDispatcher::SelectNextModel()
{
    if (!initialized_ || scheduler_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.dispatcher.select_next"};
    }
    common::Error status = scheduler_->SelectNext();
    if (!status.Ok()) {
        last_error_ = status.detail;
        return status;
    }
    initialized_ = false;
    status = ConfigureCurrentModel();
    if (!status.Ok()) {
        last_error_ = status.detail;
        return status;
    }
    initialized_ = true;
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "ai: model switched to %s\n"),
                 reinterpret_cast<const UB *>(
                     scheduler_->GetDescriptor()->name));
    return {common::ErrorCode::kOk, 0U, "ai.dispatcher.select_next"};
}

models::ModelKind InferenceDispatcher::CurrentModel() const
{
    return scheduler_ != nullptr ? scheduler_->CurrentModel()
                                 : models::ModelKind::kPerson;
}

const models::ModelDescriptor *InferenceDispatcher::CurrentDescriptor() const
{
    return scheduler_ != nullptr ? scheduler_->GetDescriptor() : nullptr;
}

const npu::Status &InferenceDispatcher::LastNpuStatus() const
{
    static const npu::Status kEmptyStatus{};
    return scheduler_ != nullptr ? scheduler_->LastStatus() : kEmptyStatus;
}

common::Error InferenceDispatcher::ConfigureCurrentModel()
{
    if (scheduler_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.dispatcher.configure"};
    }
    const models::ModelDescriptor *descriptor = scheduler_->GetDescriptor();
    if (descriptor == nullptr) {
        return {common::ErrorCode::kModel, 0U, "ai.model_descriptor"};
    }
    common::Error status = scheduler_->GetInfo(&info_);
    if (!status.Ok() || info_.n_inputs != 1U || info_.inputs == nullptr ||
        info_.outputs == nullptr || info_.n_outputs == 0U ||
        info_.n_outputs > models::kMaxModelOutputs) {
        return {common::ErrorCode::kModel, status.detail, "ai.model_info"};
    }
    if (info_.inputs[0].size_bytes !=
        static_cast<std::size_t>(descriptor->input_width) *
            descriptor->input_height * 3U) {
        return {common::ErrorCode::kModel, 0U, "ai.model_input_shape"};
    }

    stai_size output_count = 0U;
    status = scheduler_->GetOutputs(outputs_, &output_count);
    if (!status.Ok() || output_count != info_.n_outputs) {
        return {common::ErrorCode::kModel, status.detail, "ai.model_outputs"};
    }

    /* NULL outputs identify the application-owned output path. */
    dynamic_outputs_ = false;
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        const bool output_is_user_owned = outputs_[i] == nullptr;
        if (i == 0U) {
            dynamic_outputs_ = output_is_user_owned;
        } else if (dynamic_outputs_ != output_is_user_owned) {
            return {common::ErrorCode::kModel, i, "ai.mixed_output_ownership"};
        }
    }

    status = scheduler_->ConfigureActiveModel(BuildOutputSpec(info_));
    if (!status.Ok()) {
        return status;
    }
    last_error_ = 0U;
    return {common::ErrorCode::kOk, 0U, "ai.dispatcher.configure"};
}

common::Error InferenceDispatcher::TryInfer(
    const memory_allocator::InferenceFrame &frame,
    memory_allocator::BoxSet *result)
{
    if (!initialized_ || scheduler_ == nullptr || memory_ == nullptr ||
        cache_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U, "ai.infer"};
    }
    if (!frame || result == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U, "ai.infer"};
    }

    result->person = {};
    result->face = {};
    result->segmentation = {};
    result->person_valid = false;
    result->face_valid = false;
    result->segmentation_valid = false;
    result->capture_sequence = frame.capture_sequence;
    result->model_sequence = ++model_sequence_;

    const models::ModelDescriptor *descriptor = scheduler_->GetDescriptor();
    if (descriptor == nullptr) {
        return {common::ErrorCode::kModel, 0U, "ai.model_descriptor"};
    }
    if (frame.buffer.size < info_.inputs[0].size_bytes) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: input rejected buffer=%u required=%u\n"),
                     static_cast<unsigned int>(frame.buffer.size),
                     static_cast<unsigned int>(info_.inputs[0].size_bytes));
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(frame.buffer.size),
                "ai.direct_input"};
    }
    if (!frame.from_pipe2) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: input rejected source_not_pipe2 model=%s\n"),
                     reinterpret_cast<const UB *>(descriptor->name));
        return {common::ErrorCode::kInvalidArgument, 2U, "ai.direct_input"};
    }

    const memory_allocator::Buffer input_buffer{
        frame.buffer.address, info_.inputs[0].size_bytes, frame.buffer.index,
        memory_allocator::Region::kInference};
    common::Error status = frame.input_prepared_by_cpu
                               ? cache_->PrepareForPeripheralRead(input_buffer)
                               : frame.from_pipe2
                                     ? cache_->PrepareForCpuRead(input_buffer)
                                     : cache_->PrepareForPeripheralRead(
                                           input_buffer);
    if (!status.Ok()) {
        return status;
    }
    status = scheduler_->SetInput(reinterpret_cast<stai_ptr>(frame.buffer.address),
                                  info_.inputs[0].size_bytes);
    if (!status.Ok()) {
        return status;
    }

    if (dynamic_outputs_) {
        if (frame.output_count < info_.n_outputs) {
            return {common::ErrorCode::kInvalidArgument, frame.output_count,
                    "ai.dynamic_output_count"};
        }
        stai_ptr dynamic_output_ptrs[memory_allocator::kMaxModelOutputs]{};
        for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
            const memory_allocator::Buffer &output = frame.outputs[i];
            if (!output || output.size < info_.outputs[i].size_bytes ||
                output.alignment == 0U ||
                (output.address % output.alignment) != 0U) {
                return {common::ErrorCode::kInvalidArgument,
                        static_cast<std::uint32_t>(i),
                        "ai.dynamic_output_buffer"};
            }
            dynamic_output_ptrs[i] =
                reinterpret_cast<stai_ptr>(output.address);
        }
        status = scheduler_->SetOutputs(dynamic_output_ptrs, info_.n_outputs);
        if (!status.Ok()) {
            return status;
        }
        for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
            outputs_[i] = dynamic_output_ptrs[i];
        }
    }

    status = scheduler_->Run();
    last_error_ = status.detail;
    if (!status.Ok()) {
        return status;
    }
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        const memory_allocator::Buffer output_buffer =
            dynamic_outputs_
                ? frame.outputs[i]
                : memory_allocator::Buffer{
                      reinterpret_cast<std::uintptr_t>(outputs_[i]),
                      info_.outputs[i].size_bytes, 0U,
                      memory_allocator::Region::kInference};
        status = cache_->PrepareForCpuRead(output_buffer);
        if (!status.Ok()) {
            return status;
        }
    }

    models::ModelOutputView output_view{};
    output_view.count = info_.n_outputs;
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        output_view.tensors[i].data = outputs_[i];
        output_view.tensors[i].spec.size_bytes = info_.outputs[i].size_bytes;
        output_view.tensors[i].spec.scale = info_.outputs[i].scale.data[0];
        output_view.tensors[i].spec.zero_point =
            info_.outputs[i].zeropoint.data[0];
    }
    const models::InferenceGeometry geometry =
        BuildInferenceGeometry(*descriptor, frame.from_pipe2);
    const models::InferenceCompletionContext context{output_view, geometry};
    models::ModelResult decoded_result{};
    status = scheduler_->DecodeActive(context, &decoded_result);
    if (!status.Ok()) {
        return status;
    }

    switch (scheduler_->CurrentModel()) {
    case models::ModelKind::kPerson:
        if (decoded_result.kind != models::ModelKind::kPerson ||
            !decoded_result.detections_valid) {
            return {common::ErrorCode::kModel, 0U, "ai.person_decoder"};
        }
        CopyDetections(decoded_result, &result->person);
        result->person_valid = true;
        break;
    case models::ModelKind::kFace:
        if (decoded_result.kind != models::ModelKind::kFace ||
            !decoded_result.detections_valid) {
            return {common::ErrorCode::kModel, 0U, "ai.face_decoder"};
        }
        CopyDetections(decoded_result, &result->face);
        result->face_valid = true;
        break;
    case models::ModelKind::kSegmentation:
        if (decoded_result.kind != models::ModelKind::kSegmentation ||
            !decoded_result.segmentation_valid) {
            return {common::ErrorCode::kModel, 0U,
                    "ai.segmentation_decoder"};
        }
        result->segmentation.mask_address =
            decoded_result.segmentation.mask_address;
        result->segmentation.mask_width =
            decoded_result.segmentation.mask_width;
        result->segmentation.mask_height =
            decoded_result.segmentation.mask_height;
        result->segmentation.mask_foreground_pixels =
            decoded_result.segmentation.mask_foreground_pixels;
        result->segmentation_valid = true;
        break;
    }

    status = scheduler_->NewInference();
    if (!status.Ok()) {
        return status;
    }
    const std::uint32_t result_count =
        result->person.count + result->face.count;
    return {common::ErrorCode::kOk, result_count, "ai.infer"};
}

} // namespace uai::ai
