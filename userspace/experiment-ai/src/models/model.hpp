#pragma once

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "stai.h"

namespace uai::ai::cache {
class CacheDriver;
}

namespace uai::ai::memory_allocator {
struct BoxSet;
struct InferenceFrame;
}

namespace uai::ai::models {

constexpr std::size_t kMaxModelOutputs = 4U;
constexpr std::size_t kMaxDecodedDetections = 16U;

/* Identifies the model selected by the application and NPU scheduler. */
enum class ModelKind : std::uint8_t {
    kPerson,
    kSegmentation,
    kFace,
};

/* A model pipeline names operations at the granularity that matters for
 * profiling.  The NPU protocol stages are present in the same description as
 * CPU stages, but their implementation belongs to the runtime/driver. */
enum class ModelStageId : std::uint8_t {
    kCopy,
    kResize,
    kLetterbox,
    kInputCache,
    kSubmit,
    kIrqWait,
    kEpochContinue,
    kOutputCache,
    kDecode,
    kConvert,
    kFinalize,
};

enum class ModelStageLocation : std::uint8_t {
    kInferenceTaskCpu,
    kNpuHardware,
    kNpuIrq,
};

enum class ModelStageOverlap : std::uint8_t {
    kSerial,
    kMayOverlap,
};

struct ModelStageDescriptor {
    ModelStageId id = ModelStageId::kCopy;
    const char *name = "";
    ModelStageLocation location = ModelStageLocation::kInferenceTaskCpu;
    ModelStageOverlap overlap = ModelStageOverlap::kSerial;
    const char *processing = "";
};

struct ModelPipeline {
    const ModelStageDescriptor *stages = nullptr;
    std::size_t count = 0U;
};

/* Public model metadata shared by camera setup and input preparation. Output
 * tensor shapes, quantization, command blobs, and runtime buffers stay in the
 * runtime/decoder layers. Each concrete model owns and returns its descriptor. */
struct ModelDescriptor {
    ModelKind kind;               // Logical model identifier.
    const char *name;             // Human-readable model name for logs.
    std::uint32_t input_width;    // Input tensor width in pixels.
    std::uint32_t input_height;   // Input tensor height in pixels.
};

/* Model-neutral tensor metadata passed to a model's decoder. This deliberately
 * contains no STAI types; the runtime adapter builds it from stai_network_info. */
struct TensorSpec {
    std::size_t size_bytes = 0U;
    float scale = 1.0F;
    std::int32_t zero_point = 0;
};

struct ModelOutputSpec {
    TensorSpec tensors[kMaxModelOutputs]{};
    std::uint16_t count = 0U;
};

struct TensorView {
    const void *data = nullptr;
    TensorSpec spec{};
};

struct ModelOutputView {
    TensorView tensors[kMaxModelOutputs]{};
    std::uint16_t count = 0U;
};

enum class InputProjection : std::uint8_t {
    kLetterboxed,
    kCenteredSquare,
};

/* Geometry is expressed as a generic image projection, not as a DCMIPP or
 * Pipe2 detail. It lets a decoder return frame-space results without knowing
 * which camera peripheral produced the input. */
struct InferenceGeometry {
    InputProjection projection = InputProjection::kLetterboxed;
    std::uint32_t frame_width = 0U;
    std::uint32_t frame_height = 0U;
    std::uint32_t model_width = 0U;
    std::uint32_t model_height = 0U;
    std::uint32_t content_height = 0U;
    std::uint32_t pad_top = 0U;
};

struct Detection {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float confidence = 0.0F;
    std::int32_t class_index = 0;
};

struct SegmentationResult {
    std::uintptr_t mask_address = 0U;
    std::uint16_t mask_width = 0U;
    std::uint16_t mask_height = 0U;
    std::uint32_t mask_foreground_pixels = 0U;
};

struct ModelResult {
    ModelKind kind = ModelKind::kPerson;
    bool detections_valid = false;
    std::uint32_t detection_count = 0U;
    Detection detections[kMaxDecodedDetections]{};
    bool segmentation_valid = false;
    SegmentationResult segmentation{};
};

/* Scratch context passed to a model-owned stage. Runtime protocol stages do
 * not need this interface; they operate through NpuDriver. */
struct ModelStageContext {
    memory_allocator::InferenceFrame *frame = nullptr;
    cache::CacheDriver *cache = nullptr;
    ModelOutputView *outputs = nullptr;
    InferenceGeometry *geometry = nullptr;
    ModelResult *decoded_result = nullptr;
    memory_allocator::BoxSet *destination = nullptr;
};

struct InferenceCompletionContext {
    const ModelOutputView &outputs;
    const InferenceGeometry &geometry;
};

using ModelConfigureCallback = common::Error (*) (
    const ModelOutputSpec &spec, void *user_data);
using ModelCompletionCallback = common::Error (*) (
    const InferenceCompletionContext &context, ModelResult *result,
    void *user_data);

struct ModelCallbacks {
    ModelConfigureCallback configure = nullptr;
    ModelCompletionCallback on_inference_complete = nullptr;
    void *user_data = nullptr;
};

/* Raw epoch timing events are kept at the generated-runtime boundary.  The
 * callback is intentionally model-neutral so the scheduler can profile every
 * generated network without exposing an ST Edge AI context to application
 * code. */
using EpochTraceCallback = void (*) (
    void *context, std::uint32_t callback_type, std::uint32_t epoch_index,
    std::uint32_t epoch_flags, std::uintptr_t epoch_address);

/* Internal STAI bridge used only by the NPU driver/scheduler. Application
 * code should depend on Model, not on these generated-runtime operations. */
class ModelRuntime {
public:
    virtual stai_return_code Initialize() = 0;
    virtual stai_return_code Shutdown() = 0;
    virtual stai_return_code GetInfo(stai_network_info *info) = 0;
    virtual stai_return_code GetInputs(stai_ptr *inputs, stai_size *count) = 0;
    virtual stai_return_code SetInput(stai_ptr input, stai_size size) = 0;
    virtual stai_return_code GetOutputs(stai_ptr *outputs,
                                        stai_size *count) = 0;
    virtual stai_return_code SetOutputs(const stai_ptr *outputs,
                                        stai_size count) = 0;
    virtual stai_return_code Run(stai_run_mode mode) = 0;
    virtual stai_return_code ContinueRun() = 0;
    virtual stai_return_code WaitForEvent() = 0;
    virtual stai_return_code GetRunStatus() = 0;
    virtual stai_return_code NewInference() = 0;
    virtual stai_return_code SetEpochTraceCallback(EpochTraceCallback callback,
                                                   void *context) = 0;

protected:
    virtual ~ModelRuntime() = default;
};

/* Application-facing model contract. */
class Model {
public:
    /* Returns the static contract owned by this concrete model. */
    virtual const ModelDescriptor &GetDescriptor() const = 0;
    /* Returns the concrete operation-level pipeline for this model. */
    virtual const ModelPipeline &GetPipeline() const = 0;
    /* Executes a model-owned CPU stage. Runtime/NPU stages are dispatched by
     * InferenceDispatcher and must not be implemented here. */
    virtual common::Error ExecuteStage(ModelStageId stage,
                                       ModelStageContext &context) const = 0;
    /* Returns optional model-specific output lifecycle hooks. */
    virtual ModelCallbacks GetCallbacks() const
    {
        return {};
    }
    /* Model-specific input preparation belongs to the concrete model. */
    virtual common::Error PrepareInput(memory_allocator::InferenceFrame &frame,
                                       cache::CacheDriver &cache) const = 0;
    /* Model-specific conversion into the application result contract. */
    virtual common::Error ConvertResult(
        const ModelResult &source, memory_allocator::BoxSet *destination) const = 0;

protected:
    static common::Error ExecutePipe2InputStage(ModelStageId stage,
                                                ModelStageContext &context,
                                                std::uint32_t model_width,
                                                std::uint32_t model_height);
    /* Shared implementation for models whose input is a letterboxed view of
     * the camera's Pipe2 frame. This is intentionally not public model API. */
    static common::Error PreparePipe2LetterboxedInput(
        memory_allocator::InferenceFrame &frame, std::uint32_t model_width,
        std::uint32_t model_height, cache::CacheDriver &cache);
    virtual ~Model() = default;
};

/* A model registration owned by the scheduler's model facade. The generated
 * runtime adapter remains private to the scheduler path, while the concrete
 * model supplies its descriptor and decoder callbacks. */
struct ModelBinding {
    ModelKind kind{};
    Model *model = nullptr;
    ModelRuntime *runtime = nullptr;
};

} // namespace uai::ai::models
