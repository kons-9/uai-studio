#include "model_manager/model/face/model_face_adapter.hpp"

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

namespace uai::ai::model_manager {

stai_return_code FaceModelAdapter::Initialize() { return face_model_initialize(); }
stai_return_code FaceModelAdapter::Shutdown() { return face_model_shutdown(); }
stai_return_code FaceModelAdapter::GetInfo(stai_network_info *info)
{
    return face_model_get_info(info);
}
stai_return_code FaceModelAdapter::GetInputs(stai_ptr *inputs, stai_size *count)
{
    return face_model_get_inputs(inputs, count);
}
stai_return_code FaceModelAdapter::SetInput(stai_ptr input, stai_size size)
{
    return face_model_set_input(input, size);
}
stai_return_code FaceModelAdapter::GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return face_model_get_outputs(outputs, count);
}
stai_return_code FaceModelAdapter::SetOutputs(const stai_ptr *outputs,
                                              stai_size count)
{
    return face_model_set_outputs(outputs, count);
}
stai_return_code FaceModelAdapter::Run(stai_run_mode mode)
{
    return face_model_run(mode);
}
stai_return_code FaceModelAdapter::ContinueRun() { return face_model_continue_run(); }
stai_return_code FaceModelAdapter::WaitForEvent() { return face_model_wait_for_event(); }
stai_return_code FaceModelAdapter::GetRunStatus() { return face_model_get_run_status(); }
stai_return_code FaceModelAdapter::NewInference() { return face_model_new_inference(); }

} // namespace uai::ai::model_manager
