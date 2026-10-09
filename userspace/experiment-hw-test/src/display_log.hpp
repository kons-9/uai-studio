#pragma once

#include <cstddef>
#include <cstdint>
#include "middleware/ui/touch_point.hpp"

extern "C" {
extern std::uint16_t experiment_hwtest_display_framebuffer[800U * 480U];
}

namespace experiment::hwtest::display_log {

bool Initialize();
bool Ready();
bool SelfTest();
void Write(const char *text);
void Begin(unsigned expected_total = 0);
void Selection(const char *name);
void Progress(const char *name, unsigned current, unsigned total);
std::uintptr_t FramebufferAddress();
bool IsFramebuffer(std::uintptr_t address);
void Target(unsigned index);

enum class ActionKind { kNone, kSelect, kRun, kStop };
struct Action {
    ActionKind kind = ActionKind::kNone;
    std::size_t selection = 0;
};
Action Process(const std::uint16_t *pipe1, const std::uint16_t *pipe2,
               const uai::ai::ui::TouchPoint &touch, std::uint32_t first, std::uint32_t second,
               std::size_t selection, std::size_t choice_count);

} // namespace experiment::hwtest::display_log
