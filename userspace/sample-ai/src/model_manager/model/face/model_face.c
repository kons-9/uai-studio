#define stai_network_init face_stai_network_init
#define stai_network_deinit face_stai_network_deinit
#define stai_network_run face_stai_network_run
#define stai_network_train face_stai_network_train
#define stai_network_get_info face_stai_network_get_info
#define stai_network_get_inputs face_stai_network_get_inputs
#define stai_network_get_weights face_stai_network_get_weights
#define stai_network_get_outputs face_stai_network_get_outputs
#define stai_network_get_activations face_stai_network_get_activations
#define stai_network_get_states face_stai_network_get_states
#define stai_network_get_error face_stai_network_get_error
#define stai_network_set_inputs face_stai_network_set_inputs
#define stai_network_set_weights face_stai_network_set_weights
#define stai_network_set_outputs face_stai_network_set_outputs
#define stai_network_set_activations face_stai_network_set_activations
#define stai_network_set_states face_stai_network_set_states
#define stai_network_set_callback face_stai_network_set_callback
#define stai_ext_network_run_continue face_stai_ext_network_run_continue
#define stai_ext_network_get_nn_run_status face_stai_ext_network_get_nn_run_status
#define stai_ext_network_new_inference face_stai_ext_network_new_inference

#define LL_ATON_WeightEncryption_Info_network face_LL_ATON_WeightEncryption_Info_network
#define LL_ATON_BlobEncryption_Info_network face_LL_ATON_BlobEncryption_Info_network
#define LL_ATON_Set_User_Input_Buffer_network face_LL_ATON_Set_User_Input_Buffer_network
#define LL_ATON_Get_User_Input_Buffer_network face_LL_ATON_Get_User_Input_Buffer_network
#define LL_ATON_Set_User_Output_Buffer_network face_LL_ATON_Set_User_Output_Buffer_network
#define LL_ATON_Get_User_Output_Buffer_network face_LL_ATON_Get_User_Output_Buffer_network
#define LL_ATON_EpochBlockItems_network face_LL_ATON_EpochBlockItems_network
#define LL_ATON_Input_Buffers_Info_network face_LL_ATON_Input_Buffers_Info_network
#define LL_ATON_Output_Buffers_Info_network face_LL_ATON_Output_Buffers_Info_network
#define LL_ATON_Internal_Buffers_Info_network face_LL_ATON_Internal_Buffers_Info_network
#define LL_ATON_EC_Network_Init_network face_LL_ATON_EC_Network_Init_network
#define LL_ATON_EC_Inference_Init_network face_LL_ATON_EC_Inference_Init_network

#include "../../../../models/face/network.c"
#include "../../../../models/face/stai_network.c"

extern stai_return_code stai_ext_wfe(void);

STAI_NETWORK_CONTEXT_DECLARE(face_context, STAI_NETWORK_CONTEXT_SIZE)

stai_return_code face_model_initialize(void)
{
    return face_stai_network_init(face_context);
}

stai_return_code face_model_shutdown(void)
{
    return face_stai_network_deinit(face_context);
}

stai_return_code face_model_get_info(stai_network_info *info)
{
    return face_stai_network_get_info(face_context, info);
}

stai_return_code face_model_get_inputs(stai_ptr *inputs, stai_size *count)
{
    return face_stai_network_get_inputs(face_context, inputs, count);
}

stai_return_code face_model_set_input(stai_ptr input, stai_size size)
{
    return face_LL_ATON_Set_User_Input_Buffer_network(
               0U, input, size) == LL_ATON_User_IO_NOERROR
               ? STAI_SUCCESS
               : STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS;
}

stai_return_code face_model_get_outputs(stai_ptr *outputs, stai_size *count)
{
    return face_stai_network_get_outputs(face_context, outputs, count);
}

stai_return_code face_model_set_outputs(const stai_ptr *outputs,
                                        stai_size count)
{
    return face_stai_network_set_outputs(face_context, outputs, count);
}

stai_return_code face_model_run(stai_run_mode mode)
{
    return face_stai_network_run(face_context, mode);
}

stai_return_code face_model_continue_run(void)
{
    return face_stai_ext_network_run_continue(face_context);
}

stai_return_code face_model_wait_for_event(void)
{
    return stai_ext_wfe();
}

stai_return_code face_model_get_run_status(void)
{
    return face_stai_ext_network_get_nn_run_status(face_context);
}

stai_return_code face_model_new_inference(void)
{
    return face_stai_ext_network_new_inference(face_context);
}
