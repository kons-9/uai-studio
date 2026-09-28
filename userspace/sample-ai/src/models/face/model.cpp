#include "models/face/model.hpp"

extern "C" {
stai_return_code face_model_initialize(void);
stai_return_code face_model_shutdown(void);
stai_return_code face_model_get_info(stai_network_info *info);
stai_return_code face_model_get_inputs(stai_ptr *inputs, stai_size *count);
stai_return_code face_model_set_input(stai_ptr input, stai_size size);
stai_return_code face_model_get_outputs(stai_ptr *outputs, stai_size *count);
stai_return_code face_model_set_outputs(const stai_ptr *outputs,
                                        stai_size count);
stai_return_code face_model_run(stai_run_mode mode);
stai_return_code face_model_continue_run(void);
stai_return_code face_model_wait_for_event(void);
stai_return_code face_model_get_run_status(void);
stai_return_code face_model_new_inference(void);
}

namespace uai::ai::models::face {

const ModelDescriptor &Model::Descriptor()
{
    static constexpr ModelDescriptor kDescriptor{
        ModelKind::kFace, "face", 128U, 128U, 4U,
        {512U * 16U, 512U, 384U, 384U * 16U}, true};
    return kDescriptor;
}

const ModelDescriptor &Model::GetDescriptor() const
{
    return Descriptor();
}

stai_return_code Model::Initialize()
{
    return face_model_initialize();
}

stai_return_code Model::Shutdown()
{
    return face_model_shutdown();
}

stai_return_code Model::GetInfo(stai_network_info *info)
{
    return face_model_get_info(info);
}

stai_return_code Model::GetInputs(stai_ptr *inputs, stai_size *count)
{
    return face_model_get_inputs(inputs, count);
}

stai_return_code Model::SetInput(stai_ptr input, stai_size size)
{
    return face_model_set_input(input, size);
}

stai_return_code Model::GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return face_model_get_outputs(outputs, count);
}

stai_return_code Model::SetOutputs(const stai_ptr *outputs, stai_size count)
{
    return face_model_set_outputs(outputs, count);
}

stai_return_code Model::Run(stai_run_mode mode)
{
    return face_model_run(mode);
}

stai_return_code Model::ContinueRun()
{
    return face_model_continue_run();
}

stai_return_code Model::WaitForEvent()
{
    return face_model_wait_for_event();
}

stai_return_code Model::GetRunStatus()
{
    return face_model_get_run_status();
}

stai_return_code Model::NewInference()
{
    return face_model_new_inference();
}

} // namespace uai::ai::models::face
