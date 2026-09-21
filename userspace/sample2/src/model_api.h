#ifndef MODEL_API_H
#define MODEL_API_H

#include <stdint.h>

#include "stai.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct model_api
    {
        const char *name;
        uint32_t input_width;
        uint32_t input_height;
        uint32_t input_channels;
        stai_return_code (*init)(void);
        stai_return_code (*deinit)(void);
        stai_return_code (*get_info)(stai_network_info *info);
        stai_return_code (*get_error)(void);
        stai_return_code (*get_inputs)(stai_ptr *inputs, stai_size *count);
        stai_return_code (*get_outputs)(stai_ptr *outputs, stai_size *count);
        stai_return_code (*run)(stai_run_mode mode);
        stai_return_code (*run_continue)(void);
        stai_return_code (*wfe)(void);
        stai_return_code (*get_run_status)(void);
        stai_return_code (*new_inference)(void);
    } model_api;

    extern const model_api person_model;

#ifdef __cplusplus
}
#endif

#endif /* MODEL_API_H */
