#include "driver/touch_driver/touch_driver.hpp"

#include "middleware/foundation/log.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_bus.h"
#include "stm32n6570_discovery_ts.h"
#include "gt911.h"
}

namespace uai::ai::touch {
namespace {

constexpr std::uint16_t kTouchWidth = 800U;
constexpr std::uint16_t kTouchHeight = 480U;

/* The controller object is a C struct from the ST component driver; keep it
 * out of the header so users of TouchManagement do not pull in HAL types. */
GT911_Object_t g_controller{};

} // namespace

common::Error TouchDriver::Initialize()
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized};
    }
    common::Error management_status = management_->Initialize();
    if (!management_status.Ok() && management_status.Code() != common::ErrorCode::kAlreadyInitialized) {
        return management_status;
    }
    Writer writer;
    management_status = management_->Acquire(&writer);
    if (!management_status.Ok())
        return management_status;

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

    /* Each step is attempted only after the previous one succeeded; -999
     * marks steps that were skipped so the failure log shows how far it got. */
    std::int32_t bus_status = -999;
    std::int32_t id_status = -999;
    std::int32_t controller_status = -999;
    std::uint32_t id = 0U;
    bus_status = GT911_RegisterBusIO(&g_controller, &io);
    if (bus_status == GT911_OK) {
        id_status = GT911_ReadID(&g_controller, &id);
    }
    if (id_status == GT911_OK && id == GT911_ID) {
        controller_status = GT911_Init(&g_controller);
    }
    if (controller_status != GT911_OK) {
        UAI_LOG_WARN(
            "touch: gt911 init failed bus=%d id=%d/%x ctrl=%d\n",
            static_cast<int>(bus_status),
            static_cast<int>(id_status),
            static_cast<unsigned int>(id),
            static_cast<int>(controller_status)
        );
        return {common::ErrorCode::kHardware};
    }

    initialized_ = true;
    return {common::ErrorCode::kOk};
}

common::Error TouchDriver::Read(
    ui::TouchPoint *sample,
    const Writer &writer
)
{
    common::Error ownership = management_->Validate(writer);
    if (!ownership.Ok())
        return ownership;
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized};
    }
    if (sample == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }

    GT911_State_t state{};
    if (GT911_GetState(&g_controller, &state) != GT911_OK) {
        return {common::ErrorCode::kHardware};
    }
    sample->active = state.TouchDetected != 0U;
    if (sample->active) {
        sample->x = static_cast<std::uint16_t>(state.TouchX < kTouchWidth ? state.TouchX : kTouchWidth - 1U);
        sample->y = static_cast<std::uint16_t>(state.TouchY < kTouchHeight ? state.TouchY : kTouchHeight - 1U);
    } else {
        sample->x = 0U;
        sample->y = 0U;
    }
    return {common::ErrorCode::kOk};
}

} // namespace uai::ai::touch
