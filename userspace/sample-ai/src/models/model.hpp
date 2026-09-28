#ifndef UAI_AI_MODELS_MODEL_HPP
#define UAI_AI_MODELS_MODEL_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "stai.h"

namespace uai::ai::models {

constexpr std::size_t kMaxModelOutputs = 4U;
constexpr std::size_t kMaxDecodedDetections = 16U;

/* Identifies the model selected by the application and NPU scheduler. */
enum class ModelKind : std::uint8_t {
    kPerson,
    kSegmentation,
    kFace,
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

struct ModelResult {
    ModelKind kind = ModelKind::kPerson;
    bool detections_valid = false;
    std::uint32_t detection_count = 0U;
    Detection detections[kMaxDecodedDetections]{};
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

protected:
    virtual ~ModelRuntime() = default;
};

/* Application-facing model contract. */
class Model {
public:
    /* Returns the static contract owned by this concrete model. */
    virtual const ModelDescriptor &GetDescriptor() const = 0;
    /* Returns optional model-specific output lifecycle hooks. */
    virtual ModelCallbacks GetCallbacks() const
    {
        return {};
    }

protected:
    virtual ~Model() = default;
};

} // namespace uai::ai::models

#endif
