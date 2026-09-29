#include "npu_runtime/inference_dispatcher/inference_dispatcher.hpp"

#include "common/log.hpp"

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

namespace uai::ai::npu_runtime {

namespace {

std::uint32_t NowMs()
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}

std::uint32_t NowCycles()
{
    return *reinterpret_cast<volatile std::uint32_t *>(0xE0001004UL);
}

class PhaseTimingScope final {
public:
    explicit PhaseTimingScope(InferencePhaseTiming &timing)
        : timing_(timing)
    {
        timing_.start_ms = NowMs();
        timing_.end_ms = timing_.start_ms;
        timing_.elapsed_ms = 0U;
        timing_.valid = false;
    }

    ~PhaseTimingScope()
    {
        timing_.end_ms = NowMs();
        timing_.elapsed_ms = timing_.end_ms - timing_.start_ms;
        timing_.valid = true;
    }

private:
    InferencePhaseTiming &timing_;
};

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
    geometry.frame_width = memory_allocator::kConfig.frame_width;
    geometry.frame_height = memory_allocator::kConfig.frame_height;
    geometry.model_width = descriptor.input_width;
    geometry.model_height = descriptor.input_height;
    geometry.content_height =
        (descriptor.input_width *
             memory_allocator::kConfig.inference_source_height +
         memory_allocator::kConfig.inference_source_width - 1U) /
        memory_allocator::kConfig.inference_source_width;
    geometry.pad_top = geometry.model_height > geometry.content_height
                           ? (geometry.model_height - geometry.content_height) /
                                 2U
                           : 0U;
    return geometry;
}

} // namespace

common::Error InferenceDispatcher::Initialize(
    scheduler::Scheduler &scheduler, npu::NpuDriver &npu,
    cache::CacheDriver &cache)
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
    npu_ = &npu;
    cache_ = &cache;
    const common::Error status = ConfigureCurrentModel();
    if (!status.Ok()) {
        scheduler_ = nullptr;
        npu_ = nullptr;
        cache_ = nullptr;
        return status;
    }
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "ai.dispatcher.initialize"};
}

common::Error InferenceDispatcher::RefreshSelectedModel()
{
    if (!initialized_ || scheduler_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.dispatcher.refresh_model"};
    }
    initialized_ = false;
    const common::Error status = ConfigureCurrentModel();
    if (!status.Ok()) {
        last_error_ = status.detail;
        return status;
    }
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "ai.dispatcher.refresh_model"};
}

common::Error InferenceDispatcher::Shutdown()
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.dispatcher.shutdown"};
    }
    scheduler_ = nullptr;
    npu_ = nullptr;
    cache_ = nullptr;
    info_ = {};
    dynamic_outputs_ = false;
    initialized_ = false;
    model_sequence_ = 0U;
    last_error_ = 0U;
    last_npu_status_ = {};
    last_timing_.Reset();
    for (bool &configured : decoder_configured_) {
        configured = false;
    }
    return {common::ErrorCode::kOk, 0U, "ai.dispatcher.shutdown"};
}

common::Error InferenceDispatcher::ConfigureCurrentModel()
{
    if (scheduler_ == nullptr || npu_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.dispatcher.configure"};
    }
    const models::ModelDescriptor *descriptor = scheduler_->GetDescriptor();
    if (descriptor == nullptr) {
        return {common::ErrorCode::kModel, 0U, "ai.model_descriptor"};
    }
    last_npu_status_ = npu_->GetInfo(&info_);
    common::Error status = last_npu_status_.error;
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
    last_npu_status_ = npu_->GetOutputs(outputs_, &output_count);
    status = last_npu_status_.error;
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

    const std::size_t model_index =
        static_cast<std::size_t>(descriptor->kind);
    if (model_index >= (sizeof(decoder_configured_) /
                        sizeof(decoder_configured_[0]))) {
        return {common::ErrorCode::kModel,
                static_cast<std::uint32_t>(model_index),
                "ai.decoder.model_kind"};
    }
    if (!decoder_configured_[model_index]) {
        status = scheduler_->ConfigureActiveDecoder(BuildOutputSpec(info_));
        if (!status.Ok()) {
            return status;
        }
        decoder_configured_[model_index] = true;
    }
    last_error_ = 0U;
    return {common::ErrorCode::kOk, 0U, "ai.dispatcher.configure"};
}

common::Error InferenceDispatcher::PrepareInputFor(
    const models::ModelBinding &binding,
    memory_allocator::InferenceFrame &frame)
{
    if (!initialized_ || scheduler_ == nullptr || cache_ == nullptr) {
        return {common::ErrorCode::kNotInitialized,
                0U, "ai.dispatcher.prepare_input"};
    }
    if (!frame || binding.model == nullptr) {
        return {common::ErrorCode::kInvalidArgument,
                0U, "ai.dispatcher.prepare_input"};
    }

    const std::uint32_t start_ms = NowMs();
    models::ModelStageContext context{&frame, cache_};
    bool has_cpu_input_stage = false;
    common::Error status{};
    const models::ModelPipeline &pipeline = binding.model->GetPipeline();
    for (std::size_t i = 0U; i < pipeline.count; ++i) {
        const models::ModelStageId stage = pipeline.stages[i].id;
        if (stage != models::ModelStageId::kCopy &&
            stage != models::ModelStageId::kResize &&
            stage != models::ModelStageId::kLetterbox) {
            continue;
        }
        has_cpu_input_stage = true;
        const std::uint32_t stage_start_cycles = NowCycles();
        status = binding.model->ExecuteStage(stage, context);
        const std::uint32_t stage_end_ms = NowMs();
        const std::uint32_t stage_end_cycles = NowCycles();
        if (pipeline_stage_observer_ != nullptr) {
            pipeline_stage_observer_(
                pipeline_stage_context_, stage_end_ms,
                stage_end_cycles,
                stage_end_cycles - stage_start_cycles,
                static_cast<std::uint32_t>(binding.kind),
                static_cast<std::uint32_t>(stage));
        }
        if (!status.Ok()) {
            break;
        }
    }
    if (!has_cpu_input_stage && status.Ok()) {
        status = binding.model->PrepareInput(frame, *cache_);
    }
    const std::uint32_t end_ms = NowMs();
    if (!status.Ok()) {
        return status;
    }

    frame.input_prepared_by_cpu = has_cpu_input_stage;
    frame.input_prepared = true;
    frame.prepared_model_kind_id = static_cast<std::uint8_t>(binding.kind);
    frame.input_preparation_start_ms = start_ms;
    frame.input_preparation_end_ms = end_ms;
    frame.input_preparation_elapsed_ms = end_ms - start_ms;
    return {common::ErrorCode::kOk, 0U, "ai.dispatcher.prepare_input"};
}

void InferenceDispatcher::PreparePrefetch(void *context)
{
    auto *prefetch = static_cast<PrefetchState *>(context);
    if (prefetch == nullptr || prefetch->dispatcher == nullptr ||
        prefetch->provider == nullptr || prefetch->prepared ||
        !prefetch->error.Ok()) {
        return;
    }

    memory_allocator::InferenceFrame *frame =
        prefetch->provider(prefetch->provider_context);
    if (frame == nullptr) {
        return;
    }
    prefetch->frame = frame;
    const models::ModelBinding *binding =
        prefetch->dispatcher->scheduler_->NextBinding();
    if (binding == nullptr) {
        prefetch->error = {common::ErrorCode::kModel, 0U,
                           "ai.prefetch.next_model"};
        return;
    }
    prefetch->error = prefetch->dispatcher->PrepareInputFor(*binding, *frame);
    prefetch->prepared = prefetch->error.Ok();
}

common::Error InferenceDispatcher::ExecuteModelSelection(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr ||
        state->frame == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.model_selection.context"};
    }
    if (!state->select_model) {
        return {common::ErrorCode::kOk, 0U, "ai.pipeline.model_selection"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    PhaseTimingScope phase(
        dispatcher.last_timing_.At(InferencePhase::kModelSelection));
    common::Error status;
    if (state->frame->input_prepared) {
        const models::ModelBinding *prepared_binding = nullptr;
        for (std::size_t i = 0U; i < dispatcher.scheduler_->BindingCount();
             ++i) {
            const models::ModelBinding *binding =
                dispatcher.scheduler_->BindingAt(i);
            if (binding != nullptr &&
                static_cast<std::uint8_t>(binding->kind) ==
                    state->frame->prepared_model_kind_id) {
                prepared_binding = binding;
                break;
            }
        }
        if (prepared_binding == nullptr) {
            return {common::ErrorCode::kModel,
                    state->frame->prepared_model_kind_id,
                    "ai.npu_runtime.prepared_model"};
        }
        status = dispatcher.scheduler_->Select(prepared_binding->kind);
    } else {
        status = dispatcher.scheduler_->SelectNext();
    }
    if (!status.Ok()) {
        return status;
    }

    const models::ModelBinding *active =
        dispatcher.scheduler_->CurrentBinding();
    if (active == nullptr || active->runtime == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.npu_runtime.active_model"};
    }
    dispatcher.last_npu_status_ =
        dispatcher.npu_->SelectModel(*active->runtime);
    if (!dispatcher.last_npu_status_.Ok()) {
        return dispatcher.last_npu_status_.error;
    }
    dispatcher.npu_->SetEpochTraceModelKindId(
        static_cast<std::uint32_t>(active->kind));
    status = dispatcher.RefreshSelectedModel();
    if (!status.Ok()) {
        return status;
    }

    const models::ModelDescriptor *descriptor =
        dispatcher.scheduler_->GetDescriptor();
    if (descriptor != nullptr) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: model switched to %s\n"),
                     reinterpret_cast<const UB *>(descriptor->name));
    }
    return {common::ErrorCode::kOk, 0U, "ai.pipeline.model_selection"};
}

common::Error InferenceDispatcher::ExecuteModelCpuStage(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr ||
        state->frame == nullptr || state->pipeline == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.model_stage.context"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    const models::ModelBinding *binding =
        dispatcher.scheduler_->CurrentBinding();
    const models::ModelDescriptor *descriptor =
        dispatcher.scheduler_->GetDescriptor();
    if (binding == nullptr || binding->model == nullptr || descriptor == nullptr) {
        return {common::ErrorCode::kModel, 0U,
                "ai.pipeline.model_stage.model"};
    }

    const bool prepared_for_active_model =
        state->frame->input_prepared &&
        state->frame->prepared_model_kind_id ==
            static_cast<std::uint8_t>(descriptor->kind);
    if (state->frame->input_prepared && !prepared_for_active_model) {
        return {common::ErrorCode::kModel,
                state->frame->prepared_model_kind_id,
                "ai.input.prepared_model_mismatch"};
    }
    if (prepared_for_active_model) {
        state->input_preparation_start_ms =
            state->frame->input_preparation_start_ms;
        state->input_preparation_end_ms =
            state->frame->input_preparation_end_ms;
        state->input_preparation_elapsed_ms =
            state->frame->input_preparation_elapsed_ms;
        return {common::ErrorCode::kOk, 0U,
                "ai.pipeline.model_stage.prefetched"};
    }

    if (state->stage == models::ModelStageId::kCopy &&
        state->input_preparation_start_ms == 0U) {
        state->input_preparation_start_ms = NowMs();
    }
    models::ModelStageContext model_context{state->frame, dispatcher.cache_};
    const common::Error status =
        binding->model->ExecuteStage(state->stage, model_context);
    if (!status.Ok()) {
        return status;
    }
    if (state->stage == models::ModelStageId::kLetterbox) {
        state->frame->input_prepared_by_cpu = true;
        state->input_preparation_end_ms = NowMs();
        state->input_preparation_elapsed_ms =
            state->input_preparation_end_ms -
            state->input_preparation_start_ms;
        auto &phase = dispatcher.last_timing_.At(
            InferencePhase::kInputPreparation);
        phase.start_ms = state->input_preparation_start_ms;
        phase.end_ms = state->input_preparation_end_ms;
        phase.elapsed_ms = state->input_preparation_elapsed_ms;
        phase.valid = true;
    }
    return status;
}

common::Error InferenceDispatcher::ExecuteInputHandoff(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr ||
        state->frame == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.input_handoff.context"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    const models::ModelDescriptor *descriptor =
        dispatcher.scheduler_->GetDescriptor();
    if (descriptor == nullptr) {
        return {common::ErrorCode::kModel, 0U,
                "ai.pipeline.input_handoff.model"};
    }
    if (state->frame->buffer.size < dispatcher.info_.inputs[0].size_bytes) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: input rejected buffer=%u required=%u\n"),
                     static_cast<unsigned int>(state->frame->buffer.size),
                     static_cast<unsigned int>(dispatcher.info_.inputs[0].size_bytes));
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(state->frame->buffer.size),
                "ai.direct_input"};
    }
    if (!state->frame->from_pipe2) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: input rejected source_not_pipe2 model=%s\n"),
                     reinterpret_cast<const UB *>(descriptor->name));
        return {common::ErrorCode::kInvalidArgument, 2U,
                "ai.direct_input"};
    }

    const bool use_source = state->frame->source_valid &&
                            !state->frame->input_prepared_by_cpu;
    const memory_allocator::Buffer &input_storage =
        use_source ? state->frame->source : state->frame->buffer;
    if (input_storage.size < dispatcher.info_.inputs[0].size_bytes) {
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(input_storage.size),
                "ai.direct_input.source_buffer"};
    }
    const memory_allocator::Buffer input_buffer{
        input_storage.address, dispatcher.info_.inputs[0].size_bytes,
        input_storage.index, memory_allocator::Region::kInference};
    common::Error status = state->frame->input_prepared_by_cpu
                               ? dispatcher.cache_->PrepareForPeripheralRead(
                                     input_buffer)
                               : use_source
                                     ? dispatcher.cache_->PrepareForPeripheralRead(
                                           input_buffer)
                               : state->frame->from_pipe2
                                     ? dispatcher.cache_->PrepareForCpuRead(
                                           input_buffer)
                                     : dispatcher.cache_->PrepareForPeripheralRead(
                                           input_buffer);
    if (!status.Ok()) {
        return status;
    }
    dispatcher.last_npu_status_ = dispatcher.npu_->SetInput(
        reinterpret_cast<stai_ptr>(input_storage.address),
        dispatcher.info_.inputs[0].size_bytes);
    status = dispatcher.last_npu_status_.error;
    if (!status.Ok()) {
        return status;
    }

    if (dispatcher.dynamic_outputs_) {
        if (state->frame->output_count < dispatcher.info_.n_outputs) {
            return {common::ErrorCode::kInvalidArgument,
                    state->frame->output_count,
                    "ai.dynamic_output_count"};
        }
        stai_ptr dynamic_output_ptrs[
            memory_allocator::kConfig.model_output_bytes.size()]{};
        for (std::uint16_t i = 0U; i < dispatcher.info_.n_outputs; ++i) {
            const memory_allocator::Buffer &output = state->frame->outputs[i];
            if (!output || output.size < dispatcher.info_.outputs[i].size_bytes ||
                output.alignment == 0U ||
                (output.address % output.alignment) != 0U) {
                return {common::ErrorCode::kInvalidArgument, i,
                        "ai.dynamic_output_buffer"};
            }
            dynamic_output_ptrs[i] = reinterpret_cast<stai_ptr>(output.address);
        }
        dispatcher.last_npu_status_ = dispatcher.npu_->SetOutputs(
            dynamic_output_ptrs, dispatcher.info_.n_outputs);
        status = dispatcher.last_npu_status_.error;
        if (!status.Ok()) {
            return status;
        }
        for (std::uint16_t i = 0U; i < dispatcher.info_.n_outputs; ++i) {
            dispatcher.outputs_[i] = dynamic_output_ptrs[i];
        }
    }

    if (state->input_preparation_start_ms == 0U) {
        state->input_preparation_start_ms =
            state->frame->input_preparation_start_ms;
        state->input_preparation_end_ms =
            state->frame->input_preparation_end_ms;
        state->input_preparation_elapsed_ms =
            state->frame->input_preparation_elapsed_ms;
    }
    if (state->input_preparation_start_ms == 0U) {
        state->input_preparation_start_ms = NowMs();
        state->input_preparation_end_ms = state->input_preparation_start_ms;
    }
    const std::uint32_t input_submission_end_ms = NowMs();
    auto &input_wait_phase = dispatcher.last_timing_.At(
        InferencePhase::kInputPreparationWait);
    input_wait_phase.start_ms = state->input_preparation_end_ms;
    input_wait_phase.end_ms = input_submission_end_ms;
    input_wait_phase.elapsed_ms =
        input_submission_end_ms - state->input_preparation_end_ms;
    input_wait_phase.valid = true;
    return {common::ErrorCode::kOk, 0U, "ai.pipeline.input_handoff"};
}

common::Error InferenceDispatcher::ExecuteNpuSubmit(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.submit.context"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    dispatcher.last_npu_status_ = dispatcher.npu_->StartRun();
    if (!dispatcher.last_npu_status_.Ok()) {
        dispatcher.last_npu_status_.execution =
            dispatcher.npu_->LastExecution();
        return dispatcher.last_npu_status_.error;
    }

    state->prefetch = {&dispatcher, state->prefetch_provider,
                       state->prefetch_context};
    PreparePrefetch(&state->prefetch);
    state->npu_completed = false;
    return {common::ErrorCode::kOk, 0U, "ai.pipeline.submit"};
}

common::Error InferenceDispatcher::ExecuteNpuIrqWait(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.irq_wait.context"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    dispatcher.last_npu_status_ = dispatcher.npu_->PollRun(
        &PreparePrefetch, &state->prefetch);
    if (dispatcher.last_npu_status_.action == npu::RunAction::kWaitForIrq) {
        dispatcher.last_npu_status_ = dispatcher.npu_->WaitForIrq();
    }
    dispatcher.last_npu_status_.execution = dispatcher.npu_->LastExecution();
    const auto &execution = dispatcher.last_npu_status_.execution;
    if (execution.timing_valid) {
        auto &phase = dispatcher.last_timing_.At(
            InferencePhase::kNpuExecution);
        phase.start_ms = execution.start_ms;
        phase.end_ms = execution.end_ms;
        phase.elapsed_ms = execution.elapsed_ms;
        phase.valid = true;
    }
    if (!dispatcher.last_npu_status_.Ok()) {
        return dispatcher.last_npu_status_.error;
    }
    state->npu_completed =
        dispatcher.last_npu_status_.action == npu::RunAction::kCompleted;
    if (state->npu_completed && !state->prefetch.error.Ok()) {
        return state->prefetch.error;
    }
    return {common::ErrorCode::kOk, 0U, "ai.pipeline.irq_wait"};
}

common::Error InferenceDispatcher::ExecuteNpuEpochContinue(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.epoch_continue.context"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    dispatcher.last_npu_status_ = dispatcher.npu_->ContinueRun();
    dispatcher.last_npu_status_.execution = dispatcher.npu_->LastExecution();
    if (!dispatcher.last_npu_status_.Ok()) {
        return dispatcher.last_npu_status_.error;
    }
    if (!state->prefetch.error.Ok()) {
        return state->prefetch.error;
    }
    return {common::ErrorCode::kOk, 0U, "ai.pipeline.epoch_continue"};
}

common::Error InferenceDispatcher::ExecuteOutputPreparation(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr ||
        state->frame == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.output_preparation.context"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    PhaseTimingScope phase(
        dispatcher.last_timing_.At(InferencePhase::kOutputPreparation));
    for (std::uint16_t i = 0U; i < dispatcher.info_.n_outputs; ++i) {
        const memory_allocator::Buffer output_buffer =
            dispatcher.dynamic_outputs_
                ? state->frame->outputs[i]
                : memory_allocator::Buffer{
                      reinterpret_cast<std::uintptr_t>(dispatcher.outputs_[i]),
                      dispatcher.info_.outputs[i].size_bytes, 0U,
                      memory_allocator::Region::kInference};
        const common::Error status =
            dispatcher.cache_->PrepareForCpuRead(output_buffer);
        if (!status.Ok()) {
            return status;
        }
    }

    state->output_view.count = dispatcher.info_.n_outputs;
    for (std::uint16_t i = 0U; i < dispatcher.info_.n_outputs; ++i) {
        state->output_view.tensors[i].data = dispatcher.outputs_[i];
        state->output_view.tensors[i].spec.size_bytes =
            dispatcher.info_.outputs[i].size_bytes;
        state->output_view.tensors[i].spec.scale =
            dispatcher.info_.outputs[i].scale.data[0];
        state->output_view.tensors[i].spec.zero_point =
            dispatcher.info_.outputs[i].zeropoint.data[0];
    }
    const models::ModelDescriptor *descriptor =
        dispatcher.scheduler_->GetDescriptor();
    if (descriptor == nullptr) {
        return {common::ErrorCode::kModel, 0U,
                "ai.pipeline.output_preparation.model"};
    }
    state->geometry = BuildInferenceGeometry(*descriptor,
                                             state->frame->from_pipe2);
    return {common::ErrorCode::kOk, 0U,
            "ai.pipeline.output_preparation"};
}

common::Error InferenceDispatcher::ExecuteOutputDecoding(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.output_decoding.context"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    const models::InferenceCompletionContext completion{
        state->output_view, state->geometry};
    PhaseTimingScope phase(
        dispatcher.last_timing_.At(InferencePhase::kOutputDecoding));
    return dispatcher.scheduler_->DecodeActiveOutputs(
        completion, &state->decoded_result);
}

common::Error InferenceDispatcher::ExecuteResultConversion(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr ||
        state->result == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.result_conversion.context"};
    }

    InferenceDispatcher &dispatcher = *state->dispatcher;
    PhaseTimingScope phase(
        dispatcher.last_timing_.At(InferencePhase::kResultConversion));
    return dispatcher.scheduler_->ConvertActiveResult(state->decoded_result,
                                                       state->result);
}

common::Error InferenceDispatcher::ExecuteInferenceFinalize(void *context)
{
    auto *state = static_cast<PipelineState *>(context);
    if (state == nullptr || state->dispatcher == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.finalize.context"};
    }
    InferenceDispatcher &dispatcher = *state->dispatcher;
    dispatcher.last_npu_status_ = dispatcher.npu_->NewInference();
    if (!dispatcher.last_npu_status_.Ok()) {
        return dispatcher.last_npu_status_.error;
    }
    return {common::ErrorCode::kOk, 0U, "ai.pipeline.inference_finalize"};
}

common::Error InferenceDispatcher::ExecuteStage(
    PipelineState &state, models::ModelStageId *executed_stage_out)
{
    if (executed_stage_out == nullptr || state.pipeline == nullptr ||
        state.stage_index >= state.pipeline->count) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.stage"};
    }

    const models::ModelStageId executed_stage = state.stage;
    *executed_stage_out = executed_stage;
    const bool prefetched_input_stage =
        state.frame->input_prepared &&
        (executed_stage == models::ModelStageId::kCopy ||
         executed_stage == models::ModelStageId::kResize ||
         executed_stage == models::ModelStageId::kLetterbox);
    const std::uint32_t stage_start_cycles = NowCycles();
    bool repeat_stage = false;
    common::Error status{};
    switch (executed_stage) {
    case models::ModelStageId::kCopy:
    case models::ModelStageId::kResize:
    case models::ModelStageId::kLetterbox:
        status = ExecuteModelCpuStage(&state);
        break;
    case models::ModelStageId::kInputCache:
        status = ExecuteInputHandoff(&state);
        break;
    case models::ModelStageId::kSubmit:
        status = ExecuteNpuSubmit(&state);
        break;
    case models::ModelStageId::kIrqWait:
        status = ExecuteNpuIrqWait(&state);
        if (status.Ok() && !state.npu_completed) {
            state.stage = models::ModelStageId::kEpochContinue;
            repeat_stage = true;
        }
        break;
    case models::ModelStageId::kEpochContinue:
        status = ExecuteNpuEpochContinue(&state);
        if (status.Ok()) {
            state.stage = models::ModelStageId::kIrqWait;
            repeat_stage = true;
        }
        break;
    case models::ModelStageId::kOutputCache:
        status = ExecuteOutputPreparation(&state);
        break;
    case models::ModelStageId::kDecode:
        status = ExecuteOutputDecoding(&state);
        break;
    case models::ModelStageId::kConvert:
        status = ExecuteResultConversion(&state);
        break;
    case models::ModelStageId::kFinalize:
        status = ExecuteInferenceFinalize(&state);
        break;
    }

    const std::uint32_t stage_end_ms = NowMs();
    const std::uint32_t stage_end_cycles = NowCycles();
    if (!prefetched_input_stage && pipeline_stage_observer_ != nullptr) {
        const models::ModelDescriptor *descriptor =
            scheduler_->GetDescriptor();
        pipeline_stage_observer_(
            pipeline_stage_context_, stage_end_ms, stage_end_cycles,
            stage_end_cycles - stage_start_cycles,
            descriptor == nullptr
                ? 0xFFFFFFFFU
                : static_cast<std::uint32_t>(descriptor->kind),
            static_cast<std::uint32_t>(executed_stage));
    }
    if (!status.Ok()) {
        return status;
    }
    if (repeat_stage) {
        return {common::ErrorCode::kOk, 0U, "ai.pipeline.stage"};
    }

    /* A completed run skips the static epoch_continue marker. When the run
     * is still active, the continue case jumps back to irq_wait without
     * advancing the plan. */
    ++state.stage_index;
    if (state.stage == models::ModelStageId::kIrqWait &&
        state.npu_completed && state.stage_index < state.pipeline->count &&
        state.pipeline->stages[state.stage_index].id ==
            models::ModelStageId::kEpochContinue) {
        ++state.stage_index;
    }
    if (state.stage_index < state.pipeline->count) {
        state.stage = state.pipeline->stages[state.stage_index].id;
    }
    return {common::ErrorCode::kOk, 0U, "ai.pipeline.stage"};
}

common::Error InferenceDispatcher::BuildCompletion(
    const PipelineState &state, InferenceCompletion *completion) const
{
    if (completion == nullptr || state.frame == nullptr || scheduler_ == nullptr ||
        state.pipeline == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.pipeline.completion"};
    }
    const models::ModelBinding *binding = scheduler_->CurrentBinding();
    const models::ModelDescriptor *descriptor = scheduler_->GetDescriptor();
    if (binding == nullptr || binding->model == nullptr || descriptor == nullptr ||
        info_.outputs == nullptr || info_.n_outputs == 0U ||
        info_.n_outputs > models::kMaxModelOutputs) {
        return {common::ErrorCode::kModel, 0U,
                "ai.pipeline.completion.model"};
    }

    *completion = {};
    completion->frame = *state.frame;
    completion->model_kind = descriptor->kind;
    completion->model = binding->model;
    completion->callbacks = binding->model->GetCallbacks();
    completion->model_sequence = last_timing_.sequence;
    completion->capture_sequence = state.frame->capture_sequence;
    completion->geometry = BuildInferenceGeometry(*descriptor,
                                                   state.frame->from_pipe2);
    completion->output_view.count = info_.n_outputs;
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        completion->output_view.tensors[i].data = outputs_[i];
        completion->output_view.tensors[i].spec.size_bytes =
            info_.outputs[i].size_bytes;
        completion->output_view.tensors[i].spec.scale =
            info_.outputs[i].scale.data[0];
        completion->output_view.tensors[i].spec.zero_point =
            info_.outputs[i].zeropoint.data[0];
        completion->output_buffers[i] = state.frame->outputs[i];
        completion->output_buffers[i].address =
            reinterpret_cast<std::uintptr_t>(outputs_[i]);
        completion->output_buffers[i].size = info_.outputs[i].size_bytes;
        completion->output_buffers[i].index = state.frame->buffer.index;
        completion->output_buffers[i].region = memory_allocator::Region::kInference;
    }
    completion->execution = last_npu_status_.execution;
    completion->valid = true;
    return {common::ErrorCode::kOk, 0U, "ai.pipeline.completion"};
}

common::Error InferenceDispatcher::BeginInference(
    memory_allocator::InferenceFrame &frame, PrefetchProvider prefetch_provider,
    void *prefetch_context, bool select_model)
{
    if (!initialized_ || scheduler_ == nullptr || npu_ == nullptr ||
        cache_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.infer.begin"};
    }
    if (!frame) {
        return {common::ErrorCode::kInvalidArgument, 0U, "ai.infer.begin"};
    }
    if (pipeline_active_) {
        return {common::ErrorCode::kInvalidState, 0U,
                "ai.infer.begin.active"};
    }

    last_timing_.Reset();
    ++model_sequence_;
    last_timing_.sequence = model_sequence_;
    active_pipeline_ = {};
    active_pipeline_.dispatcher = this;
    active_pipeline_.frame = &frame;
    active_pipeline_.prefetch_provider = prefetch_provider;
    active_pipeline_.prefetch_context = prefetch_context;
    active_pipeline_.select_model = select_model;

    common::Error status = ExecuteModelSelection(&active_pipeline_);
    if (!status.Ok()) {
        active_pipeline_ = {};
        return status;
    }
    const models::ModelBinding *binding = scheduler_->CurrentBinding();
    if (binding == nullptr || binding->model == nullptr) {
        active_pipeline_ = {};
        return {common::ErrorCode::kModel, 0U,
                "ai.infer.begin.model"};
    }
    active_pipeline_.pipeline = &binding->model->GetPipeline();
    if (active_pipeline_.pipeline->stages == nullptr ||
        active_pipeline_.pipeline->count == 0U) {
        active_pipeline_ = {};
        return {common::ErrorCode::kModel, 0U,
                "ai.infer.begin.pipeline"};
    }
    active_pipeline_.stage_index = 0U;
    active_pipeline_.stage = active_pipeline_.pipeline->stages[0].id;
    pipeline_active_ = true;

    for (;;) {
        models::ModelStageId executed_stage = active_pipeline_.stage;
        status = ExecuteStage(active_pipeline_, &executed_stage);
        if (!status.Ok()) {
            pipeline_active_ = false;
            active_pipeline_ = {};
            return status;
        }
        if (executed_stage == models::ModelStageId::kSubmit) {
            return {common::ErrorCode::kOk, 0U, "ai.infer.begin"};
        }
        if (active_pipeline_.stage_index >= active_pipeline_.pipeline->count) {
            pipeline_active_ = false;
            active_pipeline_ = {};
            return {common::ErrorCode::kModel, 0U,
                    "ai.infer.begin.no_submit"};
        }
    }
}

common::Error InferenceDispatcher::WaitForInference(
    InferenceCompletion *completion)
{
    if (!pipeline_active_ || completion == nullptr) {
        return {common::ErrorCode::kInvalidState, 0U,
                "ai.infer.wait"};
    }
    common::Error status{};
    for (;;) {
        models::ModelStageId executed_stage = active_pipeline_.stage;
        status = ExecuteStage(active_pipeline_, &executed_stage);
        if (!status.Ok()) {
            pipeline_active_ = false;
            active_pipeline_ = {};
            return status;
        }
        if (executed_stage == models::ModelStageId::kIrqWait &&
            active_pipeline_.npu_completed) {
            break;
        }
        if (active_pipeline_.stage_index >= active_pipeline_.pipeline->count) {
            pipeline_active_ = false;
            active_pipeline_ = {};
            return {common::ErrorCode::kNpu, 0U,
                    "ai.infer.wait.no_completion"};
        }
    }

    status = BuildCompletion(active_pipeline_, completion);
    if (!status.Ok()) {
        pipeline_active_ = false;
        active_pipeline_ = {};
        return status;
    }
    const npu::ExecutionSnapshot execution = last_npu_status_.execution;
    status = ExecuteInferenceFinalize(&active_pipeline_);
    if (!status.Ok()) {
        completion->valid = false;
        pipeline_active_ = false;
        active_pipeline_ = {};
        return status;
    }
    completion->execution = execution;
    last_npu_status_.execution = execution;
    pipeline_active_ = false;
    active_pipeline_ = {};
    return {common::ErrorCode::kOk, 0U, "ai.infer.wait"};
}

common::Error InferenceDispatcher::CompleteInference(
    const InferenceCompletion &completion, memory_allocator::BoxSet *result)
{
    if (!initialized_ || cache_ == nullptr || !completion.valid ||
        completion.model == nullptr || result == nullptr ||
        completion.callbacks.on_inference_complete == nullptr ||
        completion.callbacks.user_data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.infer.complete"};
    }
    for (std::uint16_t i = 0U; i < completion.output_view.count; ++i) {
        const common::Error status =
            cache_->PrepareForCpuRead(completion.output_buffers[i]);
        if (!status.Ok()) {
            return status;
        }
    }

    models::ModelResult decoded{};
    const models::InferenceCompletionContext context{
        completion.output_view, completion.geometry};
    common::Error status = completion.callbacks.on_inference_complete(
        context, &decoded, completion.callbacks.user_data);
    if (!status.Ok()) {
        return status;
    }
    result->person = {};
    result->face = {};
    result->segmentation = {};
    result->person_valid = false;
    result->face_valid = false;
    result->segmentation_valid = false;
    result->capture_sequence = completion.capture_sequence;
    result->model_sequence = completion.model_sequence;
    return completion.model->ConvertResult(decoded, result);
}

common::Error InferenceDispatcher::ExecutePipeline(PipelineState &state)
{
    common::Error status = ExecuteModelSelection(&state);
    if (!status.Ok()) {
        return status;
    }
    if (state.pipeline == nullptr || state.pipeline->stages == nullptr ||
        state.pipeline->count == 0U) {
        const models::ModelBinding *binding =
            state.dispatcher->scheduler_->CurrentBinding();
        if (binding == nullptr || binding->model == nullptr) {
            return {common::ErrorCode::kModel, 0U,
                    "ai.pipeline.active_model"};
        }
        state.pipeline = &binding->model->GetPipeline();
    }
    if (state.pipeline->stages == nullptr || state.pipeline->count == 0U) {
        return {common::ErrorCode::kModel, 0U,
                "ai.pipeline.empty_model"};
    }
    state.stage_index = 0U;
    state.stage = state.pipeline->stages[0].id;
    for (;;) {
        models::ModelStageId executed_stage = state.stage;
        status = state.dispatcher->ExecuteStage(state, &executed_stage);
        if (!status.Ok()) {
            return status;
        }
        if (state.stage_index >= state.pipeline->count) {
            return {common::ErrorCode::kOk, 0U, "ai.pipeline.complete"};
        }
    }
}

common::Error InferenceDispatcher::TryInfer(
    memory_allocator::InferenceFrame &frame,
    memory_allocator::BoxSet *result,
    PrefetchProvider prefetch_provider,
    void *prefetch_context,
    bool select_model)
{
    if (!initialized_ || scheduler_ == nullptr || npu_ == nullptr ||
        cache_ == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U, "ai.infer"};
    }
    if (!frame || result == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U, "ai.infer"};
    }

    common::Error status = BeginInference(frame, prefetch_provider,
                                          prefetch_context, select_model);
    if (!status.Ok()) {
        return status;
    }
    InferenceCompletion completion{};
    status = WaitForInference(&completion);
    if (!status.Ok()) {
        return status;
    }
    status = CompleteInference(completion, result);
    if (!status.Ok()) {
        return status;
    }
    const std::uint32_t result_count =
        result->person.count + result->face.count;
    return {common::ErrorCode::kOk, result_count, "ai.infer"};
}

} // namespace uai::ai::npu_runtime
