#ifndef UAI_AI_MODELS_PERSON_C_WRAPPER_H
#define UAI_AI_MODELS_PERSON_C_WRAPPER_H

#include <stdint.h>

#include "stai.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*person_epoch_trace_callback)(void *context,
                                             uint32_t callback_type,
                                             uint32_t epoch_index,
                                             uint32_t epoch_flags,
                                             uintptr_t epoch_address);

stai_return_code person_model_initialize(void);
stai_return_code person_model_shutdown(void);
stai_return_code person_model_get_info(stai_network_info *info);
stai_return_code person_model_get_inputs(stai_ptr *inputs, stai_size *count);
stai_return_code person_model_set_input(stai_ptr input, stai_size size);
stai_return_code person_model_get_outputs(stai_ptr *outputs, stai_size *count);
stai_return_code person_model_set_outputs(const stai_ptr *outputs,
                                          stai_size count);
stai_return_code person_model_run(stai_run_mode mode);
stai_return_code person_model_continue_run(void);
stai_return_code person_model_wait_for_event(void);
stai_return_code person_model_get_run_status(void);
stai_return_code person_model_new_inference(void);
stai_return_code person_model_set_epoch_trace_callback(
    person_epoch_trace_callback callback, void *context);

#ifdef __cplusplus
}

namespace uai::ai::models::person::c_wrapper {

inline stai_return_code Initialize()
{
    return person_model_initialize();
}

inline stai_return_code Shutdown()
{
    return person_model_shutdown();
}

inline stai_return_code GetInfo(stai_network_info *info)
{
    return person_model_get_info(info);
}

inline stai_return_code SetInput(stai_ptr input, stai_size size)
{
    return person_model_set_input(input, size);
}

inline stai_return_code GetOutputs(stai_ptr *outputs, stai_size *count)
{
    return person_model_get_outputs(outputs, count);
}

inline stai_return_code SetOutputs(const stai_ptr *outputs, stai_size count)
{
    return person_model_set_outputs(outputs, count);
}

inline stai_return_code Run(stai_run_mode mode)
{
    return person_model_run(mode);
}

inline stai_return_code ContinueRun()
{
    return person_model_continue_run();
}

inline stai_return_code GetRunStatus()
{
    return person_model_get_run_status();
}

inline stai_return_code NewInference()
{
    return person_model_new_inference();
}

inline stai_return_code SetEpochTraceCallback(
    person_epoch_trace_callback callback, void *context)
{
    return person_model_set_epoch_trace_callback(callback, context);
}

} // namespace uai::ai::models::person::c_wrapper
#endif

#endif
