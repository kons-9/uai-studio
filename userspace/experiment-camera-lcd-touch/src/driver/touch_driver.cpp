#include "driver/touch_driver.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_bus.h"
#include "stm32n6570_discovery_ts.h"
}

namespace uai::camera_lcd_touch::driver {

DriverStatus TouchDriver::Initialize(TouchInitDiagnostics *diagnostics)
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }

    /* The LCD and GT911 share this reset line; release it after LCD setup. */
    TS_NRST_GPIO_CLK_ENABLE();
    GPIO_InitTypeDef reset_config{};
    reset_config.Mode = GPIO_MODE_OUTPUT_PP;
    reset_config.Pull = GPIO_PULLUP;
    reset_config.Pin = TS_NRST_PIN;
    HAL_GPIO_Init(TS_NRST_GPIO_PORT, &reset_config);
    HAL_GPIO_WritePin(TS_NRST_GPIO_PORT, TS_NRST_PIN, GPIO_PIN_SET);
    HAL_Delay(10U);

    GT911_IO_t io{};
    io.Address = TS_I2C_ADDRESS;
    io.Init = BSP_I2C2_Init;
    io.DeInit = BSP_I2C2_DeInit;
    io.ReadReg = BSP_I2C2_ReadReg16;
    io.WriteReg = BSP_I2C2_WriteReg16;
    io.GetTick = BSP_GetTick;

    const auto bus_status = GT911_RegisterBusIO(&controller_, &io);
    if (diagnostics != nullptr) {
        diagnostics->bus_status = bus_status;
    }
    if (bus_status != GT911_OK) {
        return DriverStatus::kHardwareFailure;
    }
    std::uint32_t id = 0U;
    const auto id_status = GT911_ReadID(&controller_, &id);
    if (diagnostics != nullptr) {
        diagnostics->id_status = id_status;
        diagnostics->id = id;
    }
    if (id_status != GT911_OK || id != GT911_ID) {
        return DriverStatus::kHardwareFailure;
    }
    const auto controller_status = GT911_Init(&controller_);
    if (diagnostics != nullptr) {
        diagnostics->controller_status = controller_status;
    }
    if (controller_status != GT911_OK) {
        return DriverStatus::kHardwareFailure;
    }

    initialized_ = true;
    return DriverStatus::kOk;
}

DriverStatus TouchDriver::Read(TouchSample *sample)
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    if (sample == nullptr) {
        return DriverStatus::kInvalidArgument;
    }

    GT911_State_t state{};
    if (GT911_GetState(&controller_, &state) != GT911_OK) {
        return DriverStatus::kHardwareFailure;
    }

    sample->active = state.TouchDetected != 0U;
    if (sample->active) {
        sample->x = static_cast<std::uint16_t>(
            state.TouchX < 800U ? state.TouchX : 799U);
        sample->y = static_cast<std::uint16_t>(
            state.TouchY < 480U ? state.TouchY : 479U);
    }
    return DriverStatus::kOk;
}

} // namespace uai::camera_lcd_touch::driver
