#ifndef UAI_AI_MODEL_C_API_H
#define UAI_AI_MODEL_C_API_H

#include <stdint.h>

#include "stai.h"

typedef struct ai_model_c_api {
    const char *name;
    uint32_t input_width;
    uint32_t input_height;
    uint32_t input_channels;
    stai_return_code (*init)(void);
    stai_return_code (*deinit)(void);
    stai_return_code (*get_info)(stai_network_info *info);
    stai_return_code (*get_inputs)(stai_ptr *inputs, stai_size *count);
    stai_return_code (*get_outputs)(stai_ptr *outputs, stai_size *count);
    stai_return_code (*run)(stai_run_mode mode);
    stai_return_code (*run_continue)(void);
    stai_return_code (*wfe)(void);
    stai_return_code (*get_run_status)(void);
    stai_return_code (*new_inference)(void);
} ai_model_c_api;

#endif
