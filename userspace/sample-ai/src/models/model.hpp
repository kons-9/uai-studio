#ifndef UAI_AI_MODELS_MODEL_HPP
#define UAI_AI_MODELS_MODEL_HPP

#include <cstddef>
#include <cstdint>

#include "stai.h"

namespace uai::ai::models {

/* Identifies the model selected by the application and NPU scheduler. */
enum class ModelKind : std::uint8_t {
    kPerson,
    kSegmentation,
    kFace,
};

/* Static contract shared by camera setup, input preparation, and validation of
 * the network information reported by the generated STAI model. This is
 * metadata only; weights, command blobs, and runtime buffers live elsewhere.
 * Each concrete model owns and returns its matching descriptor. */
struct ModelDescriptor {
    ModelKind kind;               // Logical model identifier.
    const char *name;             // Human-readable model name for logs.
    std::uint32_t input_width;    // Input tensor width in pixels.
    std::uint32_t input_height;   // Input tensor height in pixels.
    std::uint16_t output_count;   // Number of output tensors.
    std::size_t output_bytes[4];  // Expected byte size of each output tensor.
    bool input_from_pipe2;        // True when the input comes from DCMIPP Pipe2.
};

/* Resolves a model kind without constructing a model instance. The descriptor
 * itself remains defined by each concrete model implementation. */
const ModelDescriptor &DescriptorFor(ModelKind kind);

/* The generated ST Edge AI C API is hidden behind this C++ model interface. */
class Model {
public:
    /* Returns the static contract owned by this concrete model. */
    virtual const ModelDescriptor &GetDescriptor() const = 0;
    virtual stai_return_code Initialize() = 0;
    virtual stai_return_code Shutdown() = 0;
    virtual stai_return_code GetInfo(stai_network_info *info) = 0;
    virtual stai_return_code GetInputs(stai_ptr *inputs, stai_size *count) = 0;
    virtual stai_return_code SetInput(stai_ptr input, stai_size size) = 0;
    virtual stai_return_code GetOutputs(stai_ptr *outputs, stai_size *count) = 0;
    virtual stai_return_code SetOutputs(const stai_ptr *outputs,
                                        stai_size count) = 0;
    virtual stai_return_code Run(stai_run_mode mode) = 0;
    virtual stai_return_code ContinueRun() = 0;
    virtual stai_return_code WaitForEvent() = 0;
    virtual stai_return_code GetRunStatus() = 0;
    virtual stai_return_code NewInference() = 0;

protected:
    ~Model() = default;
};

} // namespace uai::ai::models

#endif
