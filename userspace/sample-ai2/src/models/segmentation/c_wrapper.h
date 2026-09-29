#ifndef UAI_AI_MODELS_SEGMENTATION_C_WRAPPER_H
#define UAI_AI_MODELS_SEGMENTATION_C_WRAPPER_H

#include <stdint.h>

#include "stai.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*segmentation_epoch_trace_callback)(
    void *context, uint32_t callback_type, uint32_t epoch_index,
    uint32_t epoch_flags, uintptr_t epoch_address);

stai_return_code segmentation_model_initialize(void);
stai_return_code segmentation_model_shutdown(void);
stai_return_code segmentation_model_get_info(stai_network_info *info);
stai_return_code segmentation_model_get_inputs(stai_ptr *inputs,
                                                stai_size *count);
stai_return_code segmentation_model_set_input(stai_ptr input, stai_size size);
stai_return_code segmentation_model_get_outputs(stai_ptr *outputs,
                                                 stai_size *count);
stai_return_code segmentation_model_set_outputs(const stai_ptr *outputs,
                                                 stai_size count);
stai_return_code segmentation_model_run(stai_run_mode mode);
stai_return_code segmentation_model_continue_run(void);
stai_return_code segmentation_model_wait_for_event(void);
stai_return_code segmentation_model_get_run_status(void);
stai_return_code segmentation_model_new_inference(void);
stai_return_code segmentation_model_set_epoch_trace_callback(
    segmentation_epoch_trace_callback callback, void *context);

#ifdef __cplusplus
}

namespace uai::ai::models::segmentation::c_wrapper {

inline stai_return_code Initialize()
{
    return segmentation_model_initialize();
}

inline stai_return_code Shutdown()
{
    return segmentation_model_shutdown();
}

inline stai_return_code GetInfo(stai_network_info *info)
{
    return segmentation_model_get_info(info);
}

inline stai_return_code SetInput(stai_ptr input, stai_size size)
{
    return segmentation_model_set_input(input, size);
}

inline stai_return_code GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return segmentation_model_get_outputs(outputs, count);
}

inline stai_return_code SetOutputs(const stai_ptr *outputs, stai_size count)
{
    return segmentation_model_set_outputs(outputs, count);
}

inline stai_return_code Run(stai_run_mode mode)
{
    return segmentation_model_run(mode);
}

inline stai_return_code ContinueRun()
{
    return segmentation_model_continue_run();
}

inline stai_return_code GetRunStatus()
{
    return segmentation_model_get_run_status();
}

inline stai_return_code NewInference()
{
    return segmentation_model_new_inference();
}

inline stai_return_code SetEpochTraceCallback(
    segmentation_epoch_trace_callback callback, void *context)
{
    return segmentation_model_set_epoch_trace_callback(callback, context);
}

} // namespace uai::ai::models::segmentation::c_wrapper
#endif

#endif
