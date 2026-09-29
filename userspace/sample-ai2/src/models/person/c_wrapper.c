#define ECBLOB_CONST_SECTION __attribute__((section(".network_blobs_person")))

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

#include "../../../models/person/network.c"
#include "../../../models/person/stai_network.c"

#include <stdint.h>
#include "c_wrapper.h"

typedef struct {
  person_epoch_trace_callback callback;
  void *context;
} person_epoch_trace_binding;

static person_epoch_trace_binding g_person_epoch_trace = {0};

static uint32_t person_epoch_count(void)
{
  static uint32_t count = 0U;
  if (count == 0U)
  {
    const LL_ATON_RT_EpochBlockItem_t *items =
        person_LL_ATON_EpochBlockItems_network();
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

static uint32_t person_epoch_index(const LL_ATON_RT_EpochBlockItem_t *epoch)
{
  const LL_ATON_RT_EpochBlockItem_t *items =
      person_LL_ATON_EpochBlockItems_network();
  if (epoch == NULL || items == NULL)
  {
    return UINT32_MAX;
  }

  const uintptr_t base = (uintptr_t)items;
  const uintptr_t address = (uintptr_t)epoch;
  const uintptr_t end =
      base + (uintptr_t)person_epoch_count() * sizeof(*items);
  if (address >= base && address < end &&
      ((address - base) % sizeof(*items)) == 0U)
  {
    return (uint32_t)((address - base) / sizeof(*items));
  }
  return UINT32_MAX;
}

static void person_epoch_trace_adapter(void *cookie,
                                       const stai_event_type event_type,
                                       const void *event_payload)
{
  person_epoch_trace_binding *binding =
      (person_epoch_trace_binding *)cookie;
  if (binding == NULL || binding->callback == NULL)
  {
    return;
  }

  const LL_ATON_RT_EpochBlockItem_t *epoch =
      (const LL_ATON_RT_EpochBlockItem_t *)event_payload;
  binding->callback(binding->context, (uint32_t)event_type,
                    person_epoch_index(epoch), epoch == NULL ? 0U : epoch->flags,
                    (uintptr_t)epoch);
}

/* ST AIランタイムが提供する待機関数。ここでは宣言し、実装はリンク対象の
 * ST AIランタイム側から解決する。 */
extern stai_return_code stai_ext_wfe(void);

STAI_NETWORK_CONTEXT_DECLARE(person_context, STAI_NETWORK_CONTEXT_SIZE)

stai_return_code person_model_initialize(void)
{
    return person_stai_network_init(person_context);
}

stai_return_code person_model_set_epoch_trace_callback(
    person_epoch_trace_callback callback, void *context)
{
    g_person_epoch_trace.callback = callback;
    g_person_epoch_trace.context = context;
    return person_stai_network_set_callback(
        person_context, callback == NULL ? NULL : person_epoch_trace_adapter,
        &g_person_epoch_trace);
}

stai_return_code person_model_shutdown(void)
{
    return person_stai_network_deinit(person_context);
}

stai_return_code person_model_get_info(stai_network_info *info)
{
    return person_stai_network_get_info(person_context, info);
}

stai_return_code person_model_get_inputs(stai_ptr *inputs, stai_size *count)
{
    return person_stai_network_get_inputs(person_context, inputs, count);
}

stai_return_code person_model_set_input(stai_ptr input, stai_size size)
{
    return person_LL_ATON_Set_User_Input_Buffer_network(
               0U, input, size) == LL_ATON_User_IO_NOERROR
               ? STAI_SUCCESS
               : STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS;
}

stai_return_code person_model_get_outputs(stai_ptr *outputs, stai_size *count)
{
    return person_stai_network_get_outputs(person_context, outputs, count);
}

stai_return_code person_model_set_outputs(const stai_ptr *outputs,
                                          stai_size count)
{
    return person_stai_network_set_outputs(person_context, outputs, count);
}

stai_return_code person_model_run(stai_run_mode mode)
{
    return person_stai_network_run(person_context, mode);
}

stai_return_code person_model_continue_run(void)
{
    return person_stai_ext_network_run_continue(person_context);
}

stai_return_code person_model_wait_for_event(void)
{
    return stai_ext_wfe();
}

stai_return_code person_model_get_run_status(void)
{
    return person_stai_ext_network_get_nn_run_status(person_context);
}

stai_return_code person_model_new_inference(void)
{
    return person_stai_ext_network_new_inference(person_context);
}
