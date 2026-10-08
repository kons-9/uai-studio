#pragma once
#include <stdint.h>

typedef enum { ISP_OK, ISP_ERR_STATAREA_EINVAL, ISP_ERR_WB_COLORTEMP, ISP_ERR_SENSORGAIN } ISP_StatusTypeDef;
typedef enum { EXPOSURE_TARGET_MINUS_2_0_EV = -4, EXPOSURE_TARGET_0_0_EV = 0, EXPOSURE_TARGET_PLUS_2_0_EV = 4 } ISP_ExposureCompTypeDef;
typedef struct { uint32_t X0, Y0, XSize, YSize; } ISP_StatAreaTypeDef;
typedef struct { uint32_t width, height, exposure_min, exposure_max, gain_min, gain_max; } ISP_SensorInfoTypeDef;
typedef struct {
    ISP_StatusTypeDef (*GetSensorExposure)(uint32_t, int32_t *);
    ISP_StatusTypeDef (*GetSensorGain)(uint32_t, int32_t *);
    ISP_StatusTypeDef (*SetSensorExposure)(uint32_t, int32_t);
    ISP_StatusTypeDef (*SetSensorGain)(uint32_t, int32_t);
} ISP_AppliHelpersTypeDef;
typedef struct {
    bool isInitialized;
    uint32_t cameraInstance;
    ISP_SensorInfoTypeDef sensorInfo;
    ISP_AppliHelpersTypeDef appliHelpers;
} ISP_HandleTypeDef;
#define ISP_AWB_COLORTEMP_REF 5
ISP_StatusTypeDef ISP_GetAECState(ISP_HandleTypeDef *, uint8_t *);
ISP_StatusTypeDef ISP_SetAECState(ISP_HandleTypeDef *, uint8_t);
ISP_StatusTypeDef ISP_GetExposureTarget(ISP_HandleTypeDef *, ISP_ExposureCompTypeDef *, uint32_t *);
ISP_StatusTypeDef ISP_SetExposureTarget(ISP_HandleTypeDef *, ISP_ExposureCompTypeDef);
ISP_StatusTypeDef ISP_GetStatArea(ISP_HandleTypeDef *, ISP_StatAreaTypeDef *);
ISP_StatusTypeDef ISP_SetStatArea(ISP_HandleTypeDef *, ISP_StatAreaTypeDef *);
ISP_StatusTypeDef ISP_GetWBRefMode(ISP_HandleTypeDef *, uint8_t *, uint32_t *);
ISP_StatusTypeDef ISP_SetWBRefMode(ISP_HandleTypeDef *, uint8_t, uint32_t);
ISP_StatusTypeDef ISP_ListWBRefModes(ISP_HandleTypeDef *, uint32_t *);