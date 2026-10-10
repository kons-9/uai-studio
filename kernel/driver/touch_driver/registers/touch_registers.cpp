#include "driver/touch_driver/registers/touch_registers.hpp"
#include "middleware/foundation/log.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_bus.h"
#include "stm32n6570_discovery_ts.h"
#include "gt911.h"
}

namespace uai::ai::touch::registers {
namespace {
GT911_Object_t controller{};
}

common::Error TouchRegisterLayer::Initialize()
{
    TS_NRST_GPIO_CLK_ENABLE();
    GPIO_InitTypeDef reset{};
    reset.Mode = GPIO_MODE_OUTPUT_PP;
    reset.Pull = GPIO_PULLUP;
    reset.Pin = TS_NRST_PIN;
    HAL_GPIO_Init(TS_NRST_GPIO_PORT, &reset);
    HAL_GPIO_WritePin(TS_NRST_GPIO_PORT, TS_NRST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
    GT911_IO_t io{};
    io.Address = TS_I2C_ADDRESS;
    io.Init = BSP_I2C2_Init;
    io.DeInit = BSP_I2C2_DeInit;
    io.ReadReg = BSP_I2C2_ReadReg16;
    io.WriteReg = BSP_I2C2_WriteReg16;
    io.GetTick = BSP_GetTick;
    std::uint32_t id = 0;
    const auto bus_status = GT911_RegisterBusIO(&controller, &io);
    const auto id_status = bus_status == GT911_OK ? GT911_ReadID(&controller, &id) : -999;
    const auto controller_status = id_status == GT911_OK && id == GT911_ID ? GT911_Init(&controller) : -999;
    if (controller_status != GT911_OK) {
        UAI_LOG_WARN(
            "touch: gt911 init failed bus=%d id=%d/%x ctrl=%d\n",
            static_cast<int>(bus_status),
            static_cast<int>(id_status),
            static_cast<unsigned>(id),
            static_cast<int>(controller_status)
        );
        return {common::ErrorCode::kHardware};
    }
    return {};
}

common::Error TouchRegisterLayer::Read(ui::TouchPoint &sample)
{
    GT911_State_t state{};
    if (GT911_GetState(&controller, &state) != GT911_OK)
        return {common::ErrorCode::kHardware};
    sample.active = state.TouchDetected != 0;
    sample.x = sample.active ? static_cast<std::uint16_t>(state.TouchX) : 0;
    sample.y = sample.active ? static_cast<std::uint16_t>(state.TouchY) : 0;
    return {};
}

}