#include "model_segmentation_c_api.h"
#include "model_manager/model/segmentation/model_segmentation_diagnostics.h"

#define stai_network_init segmentation_stai_network_init
#define stai_network_deinit segmentation_stai_network_deinit
#define stai_network_run segmentation_stai_network_run
#define stai_network_train segmentation_stai_network_train
#define stai_network_get_info segmentation_stai_network_get_info
#define stai_network_get_inputs segmentation_stai_network_get_inputs
#define stai_network_get_weights segmentation_stai_network_get_weights
#define stai_network_get_outputs segmentation_stai_network_get_outputs
#define stai_network_get_activations segmentation_stai_network_get_activations
#define stai_network_get_states segmentation_stai_network_get_states
#define stai_network_get_error segmentation_stai_network_get_error
#define stai_network_set_inputs segmentation_stai_network_set_inputs
#define stai_network_set_weights segmentation_stai_network_set_weights
#define stai_network_set_outputs segmentation_stai_network_set_outputs
#define stai_network_set_activations segmentation_stai_network_set_activations
#define stai_network_set_states segmentation_stai_network_set_states
#define stai_network_set_callback segmentation_stai_network_set_callback
#define stai_ext_network_run_continue segmentation_stai_ext_network_run_continue
#define stai_ext_network_get_nn_run_status segmentation_stai_ext_network_get_nn_run_status
#define stai_ext_network_new_inference segmentation_stai_ext_network_new_inference

#define LL_ATON_WeightEncryption_Info_network segmentation_LL_ATON_WeightEncryption_Info_network
#define LL_ATON_BlobEncryption_Info_network segmentation_LL_ATON_BlobEncryption_Info_network
#define LL_ATON_Set_User_Input_Buffer_network segmentation_LL_ATON_Set_User_Input_Buffer_network
#define LL_ATON_Get_User_Input_Buffer_network segmentation_LL_ATON_Get_User_Input_Buffer_network
#define LL_ATON_Set_User_Output_Buffer_network segmentation_LL_ATON_Set_User_Output_Buffer_network
#define LL_ATON_Get_User_Output_Buffer_network segmentation_LL_ATON_Get_User_Output_Buffer_network
#define LL_ATON_EpochBlockItems_network segmentation_LL_ATON_EpochBlockItems_network
#define LL_ATON_Input_Buffers_Info_network segmentation_LL_ATON_Input_Buffers_Info_network
#define LL_ATON_Output_Buffers_Info_network segmentation_LL_ATON_Output_Buffers_Info_network
#define LL_ATON_Internal_Buffers_Info_network segmentation_LL_ATON_Internal_Buffers_Info_network
#define LL_ATON_EC_Network_Init_network segmentation_LL_ATON_EC_Network_Init_network
#define LL_ATON_EC_Inference_Init_network segmentation_LL_ATON_EC_Inference_Init_network

#include "../../../../models/segmentation/network.c"
#include "../../../../models/segmentation/stai_network.c"

extern stai_return_code stai_ext_wfe(void);

STAI_NETWORK_CONTEXT_DECLARE(segmentation_context, STAI_NETWORK_CONTEXT_SIZE)

static stai_return_code segmentation_init(void)
{
    const stai_return_code code =
        segmentation_stai_network_init(segmentation_context);
#if defined(AI_SEGMENTATION_DIAG)
    if (code == STAI_SUCCESS) {
        return segmentation_stai_network_set_callback(
            segmentation_context, ai_segmentation_diag_epoch_callback,
            NULL);
    }
#endif
    return code;
}

static stai_return_code segmentation_deinit(void)
{
    return segmentation_stai_network_deinit(segmentation_context);
}

static stai_return_code segmentation_get_info(stai_network_info *info)
{
    return segmentation_stai_network_get_info(segmentation_context, info);
}

static stai_return_code segmentation_get_inputs(stai_ptr *inputs,
                                                stai_size *count)
{
    return segmentation_stai_network_get_inputs(segmentation_context, inputs,
                                                count);
}

static stai_return_code segmentation_get_outputs(stai_ptr *outputs,
                                                 stai_size *count)
{
    return segmentation_stai_network_get_outputs(segmentation_context,
                                                 outputs, count);
}

static stai_return_code segmentation_run(stai_run_mode mode)
{
    return segmentation_stai_network_run(segmentation_context, mode);
}

static stai_return_code segmentation_run_continue(void)
{
    return segmentation_stai_ext_network_run_continue(segmentation_context);
}

static stai_return_code segmentation_wfe(void)
{
    return stai_ext_wfe();
}

static stai_return_code segmentation_get_run_status(void)
{
    return segmentation_stai_ext_network_get_nn_run_status(
        segmentation_context);
}

static stai_return_code segmentation_new_inference(void)
{
    return segmentation_stai_ext_network_new_inference(segmentation_context);
}

const ai_model_c_api segmentation_model_c_api = {
    "DEEPLAB_SEGMENTATION",
    320U,
    320U,
    3U,
    segmentation_init,
    segmentation_deinit,
    segmentation_get_info,
    segmentation_get_inputs,
    segmentation_get_outputs,
    segmentation_run,
    segmentation_run_continue,
    segmentation_wfe,
    segmentation_get_run_status,
    segmentation_new_inference,
};
