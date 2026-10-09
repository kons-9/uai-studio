#include "extension.hpp"
#include "app_state.hpp"
#include "driver/frame_buffer.hpp"
extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
#include "stm32n6570_discovery_xspi.h"
#include "stm32n6570_discovery_bus.h"
#include "stm32n6570_discovery_ts.h"
#include "gt911.h"
extern LTDC_HandleTypeDef hlcd_ltdc;
}

namespace experiment {
namespace {
Services services{};
alignas(32) __attribute__((section(".experiment_panel"))) std::uint16_t pixels[800 * 480];
bool ready = false, touch_ready = false;
GT911_Object_t touch{};
scheduling::Periodic refresh;
std::uint32_t sequence = 0;

enum class FailureMode { kNone, kApply, kRollback };

class Display final : public ui::Backend {
public:
    void InjectFailure(FailureMode mode)
    {
        failure_ = mode; fail_rollback_ = false;
    }
    bool Apply(features::State next) override
    {
        if (fail_rollback_) { fail_rollback_ = false; return false; }
        const bool pipe2 = std::strcmp(features::Value(next, 0), "pipe2") == 0;
        BSP_LCD_LayerConfig_t layer{};
        layer.Address = pipe2 ? uai::camera_pipe2::driver::AncillaryPipeFrameBufferAddress() : uai::camera_pipe2::driver::MainPipeFrameBufferAddress();
        layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565; layer.X1 = 400; layer.Y1 = 480;
        const bool applied = ready && BSP_LCD_ConfigLayer(0, 0, &layer) == BSP_ERROR_NONE;
        const auto failure = failure_; failure_ = FailureMode::kNone;
        if (failure == FailureMode::kNone) { return applied; }
        fail_rollback_ = failure == FailureMode::kRollback;
        return false;
    }
private:
    FailureMode failure_ = FailureMode::kNone;
    bool fail_rollback_ = false;
};
Display display;
ui::AppState state(display);

bool ParseAction(const char *name, features::Action &action)
{
    if (!std::strcmp(name, "camera")) { action = features::Action::kShowCamera; }
    else if (!std::strcmp(name, "pipe2")) { action = features::Action::kShowPipe2; }
    else if (!std::strcmp(name, "boxes")) { action = features::Action::kToggleBoxes; }
    else { return false; }
    return true;
}

const char *TransactionName(ui::TransactionStatus status)
{
    switch (status) {
    case ui::TransactionStatus::kIdle: return "idle";
    case ui::TransactionStatus::kCommitted: return "committed";
    case ui::TransactionStatus::kRejected: return "rejected";
    case ui::TransactionStatus::kRolledBack: return "rolled-back";
    case ui::TransactionStatus::kFaulted: return "faulted";
    }
    return "unknown";
}

void Fill(unsigned left, unsigned top, unsigned width, unsigned height, std::uint16_t color)
{
    for (unsigned row = top; row < top + height && row < 480; ++row) {
        for (unsigned column = left; column < left + width && column < 800; ++column) { pixels[row * 800 + column] = color; }
    }
}

void Label(unsigned left, unsigned top, const char *text)
{
    struct Glyph { char name; std::uint8_t rows[7]; };
    static constexpr Glyph glyphs[] = {
        {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}}, {'C',{14,17,16,16,16,17,14}},
        {'E',{31,16,16,30,16,16,31}}, {'I',{31,4,4,4,4,4,31}}, {'M',{17,27,21,21,17,17,17}},
        {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}}, {'X',{17,17,10,4,10,17,17}},
        {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
        {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
        {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
        {'9',{14,17,17,15,1,1,14}}
    };
    for (; *text && left + 15 < 800; ++text, left += 18) {
        for (const auto &glyph : glyphs) {
            if (glyph.name != *text) { continue; }
            for (unsigned row = 0; row < 7; ++row) {
                for (unsigned column = 0; column < 5; ++column) {
                    if (glyph.rows[row] & (16U >> column)) { Fill(left + column * 3, top + row * 3, 3, 3, 0xffff); }
                }
            }
        }
    }
}

int Hit(unsigned horizontal, unsigned vertical)
{
    if (vertical >= 50 && vertical < 110) {
        if (horizontal >= 420 && horizontal < 580) { return static_cast<int>(features::Action::kShowCamera); }
        if (horizontal >= 610 && horizontal < 770) { return static_cast<int>(features::Action::kShowPipe2); }
    }
    if (horizontal >= 420 && horizontal < 580 && vertical >= 140 && vertical < 200) { return static_cast<int>(features::Action::kToggleBoxes); }
    return -1;
}

console::Status Command(void *, int count, const char *const *arguments, const console::Writer &writer)
{
    if (count == 2 && !std::strcmp(arguments[1], "stat")) {
        char line[224];
        std::snprintf(line, sizeof(line), "UI display=%s boxes=%s frame=%lu touch=%u ready=%u updates=%lu skipped=%llu transaction=%s faulted=%u\n",
            features::Value(state.Features(), 0), features::Value(state.Features(), 1), static_cast<unsigned long>(sequence), touch_ready, ready,
            static_cast<unsigned long>(state.Render().updates), static_cast<unsigned long long>(refresh.Statistics().skipped),
            TransactionName(state.Render().transaction), state.Render().faulted);
        writer.Write(line); return console::Status::kOk;
    }
    if (count == 2 && !std::strcmp(arguments[1], "recover")) {
        if (!state.Recover()) { return console::Status::kHardware; }
    } else if (count == 3 && !std::strcmp(arguments[1], "fail")) {
        FailureMode mode;
        if (!std::strcmp(arguments[2], "off")) { mode = FailureMode::kNone; }
        else if (!std::strcmp(arguments[2], "apply")) { mode = FailureMode::kApply; }
        else if (!std::strcmp(arguments[2], "rollback")) { mode = FailureMode::kRollback; }
        else { return console::Status::kInvalidArgument; }
        if (state.Render().faulted && mode != FailureMode::kNone) { return console::Status::kInvalidState; }
        display.InjectFailure(mode);
    } else if (count >= 3 && !std::strcmp(arguments[1], "apply")) {
        constexpr std::size_t kMaxActions = 8;
        features::Action actions[kMaxActions]{};
        const auto action_count = static_cast<std::size_t>(count - 2);
        if (action_count > kMaxActions) { return console::Status::kInvalidArgument; }
        for (std::size_t index = 0; index < action_count; ++index) {
            if (!ParseAction(arguments[index + 2], actions[index])) { return console::Status::kInvalidArgument; }
        }
        if (!state.Transact(actions, action_count)) { return state.Render().error ? console::Status::kHardware : console::Status::kInvalidState; }
    } else if (count == 7 && !std::strcmp(arguments[1], "result")) {
        std::int32_t values[5]{};
        for (int index = 2; index < count; ++index) {
            if (!camera::ParseInteger(arguments[index], values[index - 2]) || values[index - 2] < 0) { return console::Status::kInvalidArgument; }
        }
        if (!state.Publish({services.clock(), sequence}, {std::uint32_t(values[0]), std::uint32_t(values[1]), std::uint32_t(values[2]), std::uint32_t(values[3])}, values[4])) { return console::Status::kInvalidArgument; }
    } else if (count == 2) {
        features::Action action;
        if (!ParseAction(arguments[1], action)) { return console::Status::kInvalidArgument; }
        if (!state.Dispatch(action)) { return state.Render().error ? console::Status::kHardware : console::Status::kInvalidState; }
    } else { return console::Status::kInvalidArgument; }
    writer.Write("OK ui\n"); return console::Status::kOk;
}
}

std::size_t Register(const Services &provided, console::Command *commands, std::size_t capacity)
{
    services = provided;
    ready = BSP_XSPI_RAM_Init(0) == BSP_ERROR_NONE && BSP_XSPI_RAM_EnableMemoryMappedMode(0) == BSP_ERROR_NONE;
    if (ready) {
        std::memset(pixels, 0, sizeof(pixels));
        SCB_CleanDCache_by_Addr(pixels, sizeof(pixels));
        BSP_LCD_LayerConfig_t layer{};
        layer.Address = reinterpret_cast<std::uintptr_t>(pixels); layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
        layer.X1 = 800; layer.Y1 = 480;
        ready = BSP_LCD_ConfigLayer(0, 1, &layer) == BSP_ERROR_NONE &&
                HAL_LTDC_ConfigColorKeying(&hlcd_ltdc, 0, 1) == HAL_OK && HAL_LTDC_EnableColorKeying(&hlcd_ltdc, 1) == HAL_OK &&
                BSP_LCD_SetLayerVisible(0, 1, ENABLE) == BSP_ERROR_NONE;
        if (ready) { ready = display.Apply(state.Features()); }
    }
    GT911_IO_t input{};
    TS_NRST_GPIO_CLK_ENABLE();
    GPIO_InitTypeDef reset{};
    reset.Mode = GPIO_MODE_OUTPUT_PP; reset.Pull = GPIO_PULLUP; reset.Pin = TS_NRST_PIN;
    HAL_GPIO_Init(TS_NRST_GPIO_PORT, &reset);
    HAL_GPIO_WritePin(TS_NRST_GPIO_PORT, TS_NRST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
    input.Address = TS_I2C_ADDRESS; input.Init = BSP_I2C2_Init; input.DeInit = BSP_I2C2_DeInit;
    input.ReadReg = BSP_I2C2_ReadReg16; input.WriteReg = BSP_I2C2_WriteReg16; input.GetTick = BSP_GetTick;
    std::uint32_t identifier = 0;
    touch_ready = GT911_RegisterBusIO(&touch, &input) == GT911_OK && GT911_ReadID(&touch, &identifier) == GT911_OK &&
                  identifier == GT911_ID && GT911_Init(&touch) == GT911_OK;
    refresh.Configure(20, services.clock(), scheduling::LatePolicy::kLatestOnly);
    if (!capacity) { return 0; }
    commands[0] = {"ui", "ui stat|camera|pipe2|boxes|apply <actions...>|recover|fail apply|rollback|off|result <x> <y> <w> <h> <ttl-ms>", Command, nullptr};
    services.output.Write(ready ? "UI READY\n" : "UI ERROR initialization\n");
    return 1;
}

void Tick(std::uint32_t milliseconds, std::uint32_t current_sequence)
{
    sequence = current_sequence;
    if (!refresh.Take(milliseconds)) { return; }
    static unsigned last_horizontal = 0, last_vertical = 0;
    GT911_State_t input{};
    if (touch_ready && GT911_GetState(&touch, &input) == GT911_OK) {
        if (input.TouchDetected) { last_horizontal = input.TouchX; last_vertical = input.TouchY; }
        state.Touch(input.TouchDetected != 0, Hit(last_horizontal, last_vertical));
    } else { state.Touch(false, -1); }
    if (!ready) { return; }
    Fill(0, 0, 400, 480, 0); Fill(400, 0, 400, 480, state.Render().error ? 0x8000 : 0x2104);
    const auto camera_color = !state.Render().faulted && !std::strcmp(features::Value(state.Features(), 0), "camera") ? 0x0588 : 0x4208;
    const auto pipe_color = !state.Render().faulted && !std::strcmp(features::Value(state.Features(), 0), "pipe2") ? 0x0588 : 0x4208;
    const auto box_color = !state.Render().faulted && !std::strcmp(features::Value(state.Features(), 1), "on") ? 0x0588 : 0x4208;
    Fill(420, 50, 160, 60, camera_color); Label(440, 70, "CAM");
    Fill(610, 50, 160, 60, pipe_color); Label(630, 70, "PIPE2");
    Fill(420, 140, 160, 60, box_color); Label(440, 160, "BOX");
    char value[16]; std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(sequence)); Label(420, 240, value);
    if (const auto *result = state.Visible(milliseconds)) {
        const auto &rectangle = result->bounds;
        Fill(rectangle.x, rectangle.y, rectangle.width, 1, 0xffff);
        Fill(rectangle.x, rectangle.y + rectangle.height - 1, rectangle.width, 1, 0xffff);
        Fill(rectangle.x, rectangle.y, 1, rectangle.height, 0xffff);
        Fill(rectangle.x + rectangle.width - 1, rectangle.y, 1, rectangle.height, 0xffff);
    }
    SCB_CleanDCache_by_Addr(pixels, sizeof(pixels));
}
}