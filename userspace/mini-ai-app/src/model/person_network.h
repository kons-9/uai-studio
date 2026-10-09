#pragma once

#include <stdint.h>

#include "stai.h"

#ifdef __cplusplus
extern "C" {
#endif

/* C API over the generated person network (see person_network.c). */

typedef void (*person_epoch_trace_callback)(
    void *context,
    uint32_t callback_type,
    uint32_t epoch_index,
    uint32_t epoch_flags,
    uintptr_t epoch_address
);

stai_return_code person_network_init(void);
stai_return_code person_network_deinit(void);
stai_return_code person_network_get_info(stai_network_info *info);
stai_return_code person_network_set_input(
    stai_ptr input,
    stai_size size
);
stai_return_code person_network_get_outputs(
    stai_ptr *outputs,
    stai_size *count
);
stai_return_code person_network_set_outputs(
    const stai_ptr *outputs,
    stai_size count
);
stai_return_code person_network_run(stai_run_mode mode);
stai_return_code person_network_run_continue(void);
stai_return_code person_network_get_run_status(void);
stai_return_code person_network_new_inference(void);
stai_return_code person_network_set_epoch_trace_callback(
    person_epoch_trace_callback callback,
    void *context
);

#ifdef __cplusplus
}
#endif
