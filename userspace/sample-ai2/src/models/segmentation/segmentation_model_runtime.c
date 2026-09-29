#define ECBLOB_CONST_SECTION __attribute__((section(".network_blobs_segmentation")))

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

#include "../../../models/segmentation/network.c"
#include "../../../models/segmentation/stai_network.c"

#include <stdint.h>

#include "c_wrapper.h"

typedef struct {
  segmentation_epoch_trace_callback callback;
  void *context;
} segmentation_epoch_trace_binding;

static segmentation_epoch_trace_binding g_segmentation_epoch_trace = {0};

static uint32_t segmentation_epoch_count(void)
{
  static uint32_t count = 0U;
  if (count == 0U)
  {
    const LL_ATON_RT_EpochBlockItem_t *items =
        segmentation_LL_ATON_EpochBlockItems_network();
    if (items == NULL)
    {
      return 0U;
    }
    for (count = 1U; count < 4096U; ++count)
    {
      if (EpochBlock_IsLastEpochBlock(&items[count - 1U]))
      {
        break;
      }
    }
  }
  return count;
}

static uint32_t segmentation_epoch_index(
    const LL_ATON_RT_EpochBlockItem_t *epoch)
{
  const LL_ATON_RT_EpochBlockItem_t *items =
      segmentation_LL_ATON_EpochBlockItems_network();
  if (epoch == NULL || items == NULL)
  {
    return UINT32_MAX;
  }

  const uintptr_t base = (uintptr_t)items;
  const uintptr_t address = (uintptr_t)epoch;
  const uintptr_t end =
      base + (uintptr_t)segmentation_epoch_count() * sizeof(*items);
  if (address >= base && address < end &&
      ((address - base) % sizeof(*items)) == 0U)
  {
    return (uint32_t)((address - base) / sizeof(*items));
  }
  return UINT32_MAX;
}

static void segmentation_epoch_trace_adapter(
    void *cookie, const stai_event_type event_type, const void *event_payload)
{
  segmentation_epoch_trace_binding *binding =
      (segmentation_epoch_trace_binding *)cookie;
  if (binding == NULL || binding->callback == NULL)
  {
    return;
  }

  const LL_ATON_RT_EpochBlockItem_t *epoch =
      (const LL_ATON_RT_EpochBlockItem_t *)event_payload;
  binding->callback(binding->context, (uint32_t)event_type,
                    segmentation_epoch_index(epoch),
                    epoch == NULL ? 0U : epoch->flags, (uintptr_t)epoch);
}

extern stai_return_code stai_ext_wfe(void);

STAI_NETWORK_CONTEXT_DECLARE(segmentation_context, STAI_NETWORK_CONTEXT_SIZE)

stai_return_code segmentation_model_initialize(void)
{
    return segmentation_stai_network_init(segmentation_context);
}

stai_return_code segmentation_model_set_epoch_trace_callback(
    segmentation_epoch_trace_callback callback, void *context)
{
    g_segmentation_epoch_trace.callback = callback;
    g_segmentation_epoch_trace.context = context;
    return segmentation_stai_network_set_callback(
        segmentation_context,
        callback == NULL ? NULL : segmentation_epoch_trace_adapter,
        &g_segmentation_epoch_trace);
}

stai_return_code segmentation_model_shutdown(void)
{
    return segmentation_stai_network_deinit(segmentation_context);
}

stai_return_code segmentation_model_get_info(stai_network_info *info)
{
    return segmentation_stai_network_get_info(segmentation_context, info);
}

stai_return_code segmentation_model_get_inputs(stai_ptr *inputs,
                                               stai_size *count)
{
    return segmentation_stai_network_get_inputs(segmentation_context, inputs,
                                                count);
}

stai_return_code segmentation_model_set_input(stai_ptr input, stai_size size)
{
    return segmentation_LL_ATON_Set_User_Input_Buffer_network(
               0U, input, size) == LL_ATON_User_IO_NOERROR
               ? STAI_SUCCESS
               : STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS;
}

stai_return_code segmentation_model_get_outputs(stai_ptr *outputs,
                                                stai_size *count)
{
    return segmentation_stai_network_get_outputs(segmentation_context,
                                                 outputs, count);
}

stai_return_code segmentation_model_set_outputs(const stai_ptr *outputs,
                                                stai_size count)
{
    return segmentation_stai_network_set_outputs(segmentation_context,
                                                  outputs, count);
}

stai_return_code segmentation_model_run(stai_run_mode mode)
{
    return segmentation_stai_network_run(segmentation_context, mode);
}

stai_return_code segmentation_model_continue_run(void)
{
    return segmentation_stai_ext_network_run_continue(segmentation_context);
}

stai_return_code segmentation_model_wait_for_event(void)
{
    return stai_ext_wfe();
}

stai_return_code segmentation_model_get_run_status(void)
{
    return segmentation_stai_ext_network_get_nn_run_status(
        segmentation_context);
}

stai_return_code segmentation_model_new_inference(void)
{
    return segmentation_stai_ext_network_new_inference(segmentation_context);
}
