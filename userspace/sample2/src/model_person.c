#include "model_api.h"

#define stai_network_init person_stai_network_init
#define stai_network_deinit person_stai_network_deinit
#define stai_network_run person_stai_network_run
#define stai_network_train person_stai_network_train
#define stai_network_get_info person_stai_network_get_info
#define stai_network_get_inputs person_stai_network_get_inputs
#define stai_network_get_weights person_stai_network_get_weights
#define stai_network_get_outputs person_stai_network_get_outputs
#define stai_network_get_activations person_stai_network_get_activations
#define stai_network_get_states person_stai_network_get_states
#define stai_network_get_error person_stai_network_get_error
#define stai_network_set_inputs person_stai_network_set_inputs
#define stai_network_set_weights person_stai_network_set_weights
#define stai_network_set_outputs person_stai_network_set_outputs
#define stai_network_set_activations person_stai_network_set_activations
#define stai_network_set_states person_stai_network_set_states
#define stai_network_set_callback person_stai_network_set_callback
#define stai_ext_network_run_continue person_stai_ext_network_run_continue
#define stai_ext_network_get_nn_run_status person_stai_ext_network_get_nn_run_status
#define stai_ext_network_new_inference person_stai_ext_network_new_inference

#define LL_ATON_WeightEncryption_Info_network person_LL_ATON_WeightEncryption_Info_network
#define LL_ATON_BlobEncryption_Info_network person_LL_ATON_BlobEncryption_Info_network
#define LL_ATON_Set_User_Input_Buffer_network person_LL_ATON_Set_User_Input_Buffer_network
#define LL_ATON_Get_User_Input_Buffer_network person_LL_ATON_Get_User_Input_Buffer_network
#define LL_ATON_Set_User_Output_Buffer_network person_LL_ATON_Set_User_Output_Buffer_network
#define LL_ATON_Get_User_Output_Buffer_network person_LL_ATON_Get_User_Output_Buffer_network
#define LL_ATON_EpochBlockItems_network person_LL_ATON_EpochBlockItems_network
#define LL_ATON_Input_Buffers_Info_network person_LL_ATON_Input_Buffers_Info_network
#define LL_ATON_Output_Buffers_Info_network person_LL_ATON_Output_Buffers_Info_network
#define LL_ATON_Internal_Buffers_Info_network person_LL_ATON_Internal_Buffers_Info_network
#define LL_ATON_EC_Network_Init_network person_LL_ATON_EC_Network_Init_network
#define LL_ATON_EC_Inference_Init_network person_LL_ATON_EC_Inference_Init_network

#include "../models/person/network.c"
#include "../models/person/stai_network.c"

extern stai_return_code stai_ext_wfe(void);

STAI_NETWORK_CONTEXT_DECLARE(person_context, STAI_NETWORK_CONTEXT_SIZE)

static stai_return_code person_init(void)
{
    return person_stai_network_init(person_context);
}

static stai_return_code person_deinit(void)
{
    return person_stai_network_deinit(person_context);
}

static stai_return_code person_get_info(stai_network_info *info)
{
    return person_stai_network_get_info(person_context, info);
}

static stai_return_code person_get_error(void)
{
    return person_stai_network_get_error(person_context);
}

static stai_return_code person_get_inputs(stai_ptr *inputs, stai_size *count)
{
    return person_stai_network_get_inputs(person_context, inputs, count);
}

static stai_return_code person_get_outputs(stai_ptr *outputs, stai_size *count)
{
    return person_stai_network_get_outputs(person_context, outputs, count);
}

static stai_return_code person_run(stai_run_mode mode)
{
    return person_stai_network_run(person_context, mode);
}

static stai_return_code person_run_continue(void)
{
    return person_stai_ext_network_run_continue(person_context);
}

static stai_return_code person_wfe(void)
{
    return stai_ext_wfe();
}

static stai_return_code person_get_run_status(void)
{
    return person_stai_ext_network_get_nn_run_status(person_context);
}

static stai_return_code person_new_inference(void)
{
    return person_stai_ext_network_new_inference(person_context);
}

const model_api person_model = {
    "PERSON",
    480U,
    480U,
    3U,
    person_init,
    person_deinit,
    person_get_info,
    person_get_error,
    person_get_inputs,
    person_get_outputs,
    person_run,
    person_run_continue,
    person_wfe,
    person_get_run_status,
    person_new_inference,
};
