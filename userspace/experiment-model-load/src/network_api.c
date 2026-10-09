#include "model_symbols.h"
#define ECBLOB_CONST_SECTION __attribute__((section(MODEL_BLOB_SECTION)))
#include "network.c"
#include "stai_network.c"
#include "stai_ext.h"
#include <stdbool.h>
#include <stdint.h>

#define JOIN_INNER(prefix, suffix) experiment_##prefix##_##suffix
#define JOIN(prefix, suffix) JOIN_INNER(prefix, suffix)
#define API(name) JOIN(MODEL_PREFIX, name)
#define MODEL_CONTEXT JOIN(MODEL_PREFIX, model_context)

_Static_assert(STAI_NETWORK_IN_NUM == 1, "only one input tensor is supported");
_Static_assert(STAI_NETWORK_IN_1_SIZE_BYTES == MODEL_INPUT_BYTES, "input descriptor mismatch");
_Static_assert(STAI_NETWORK_OUT_NUM == MODEL_OUTPUT_COUNT, "output count mismatch");
_Static_assert(STAI_NETWORK_OUT_1_SIZE_BYTES == MODEL_OUTPUT_1_BYTES, "output 1 mismatch");
#if MODEL_OUTPUT_COUNT > 1
_Static_assert(STAI_NETWORK_OUT_2_SIZE_BYTES == MODEL_OUTPUT_2_BYTES, "output 2 mismatch");
#endif
#if MODEL_OUTPUT_COUNT > 2
_Static_assert(STAI_NETWORK_OUT_3_SIZE_BYTES == MODEL_OUTPUT_3_BYTES, "output 3 mismatch");
#endif
#if MODEL_OUTPUT_COUNT > 3
_Static_assert(STAI_NETWORK_OUT_4_SIZE_BYTES == MODEL_OUTPUT_4_BYTES, "output 4 mismatch");
#endif
#if MODEL_OUTPUT_COUNT > 4
_Static_assert(STAI_NETWORK_OUT_5_SIZE_BYTES == MODEL_OUTPUT_5_BYTES, "output 5 mismatch");
#endif
#if MODEL_OUTPUT_COUNT > 5
_Static_assert(STAI_NETWORK_OUT_6_SIZE_BYTES == MODEL_OUTPUT_6_BYTES, "output 6 mismatch");
#endif
#if MODEL_OUTPUT_COUNT > 6
_Static_assert(STAI_NETWORK_OUT_7_SIZE_BYTES == MODEL_OUTPUT_7_BYTES, "output 7 mismatch");
#endif
#if MODEL_OUTPUT_COUNT > 7
_Static_assert(STAI_NETWORK_OUT_8_SIZE_BYTES == MODEL_OUTPUT_8_BYTES, "output 8 mismatch");
#endif

STAI_NETWORK_CONTEXT_DECLARE(MODEL_CONTEXT, STAI_NETWORK_CONTEXT_SIZE)
static bool initialized;
extern bool experiment_npu_event(void);

bool API(start)(uint8_t *input, uint32_t input_bytes, uint8_t **outputs, uint32_t output_count)
{
    if (initialized || input_bytes != STAI_NETWORK_IN_1_SIZE_BYTES || output_count != STAI_NETWORK_OUT_NUM) {
        return false;
    }
    initialized = true;
    if (stai_network_init(MODEL_CONTEXT) >= STAI_ERROR_GENERIC) {
        return false;
    }
    const stai_return_code reset = stai_ext_network_new_inference(MODEL_CONTEXT);
    if (reset >= STAI_ERROR_GENERIC && reset != STAI_ERROR_NETWORK_STILL_RUNNING) {
        return false;
    }
    if (LL_ATON_Set_User_Input_Buffer_network(0, input, input_bytes) != LL_ATON_User_IO_NOERROR
        || stai_network_set_outputs(MODEL_CONTEXT, (stai_ptr *)outputs, output_count) >= STAI_ERROR_GENERIC) {
        return false;
    }
    return stai_network_run(MODEL_CONTEXT, STAI_MODE_ASYNC) < STAI_ERROR_GENERIC;
}

int API(poll)(void)
{
    if (!initialized) {
        return -1;
    }
    const stai_return_code status = stai_ext_network_get_nn_run_status(MODEL_CONTEXT);
    if (status == STAI_DONE) {
        return 1;
    }
    if (status >= STAI_ERROR_GENERIC) {
        return -1;
    }
    if (status == STAI_RUNNING_WFE && !experiment_npu_event()) {
        return 0;
    }
    if (stai_ext_network_run_continue(MODEL_CONTEXT) >= STAI_ERROR_GENERIC) {
        return -1;
    }
    return 0;
}

bool API(deinit)(void)
{
    if (!initialized) {
        return true;
    }
    if (stai_network_deinit(MODEL_CONTEXT) >= STAI_ERROR_GENERIC) {
        return false;
    }
    initialized = false;
    return true;
}
