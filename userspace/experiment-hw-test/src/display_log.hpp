#pragma once

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
void Begin();
void Selection(const char *name);
std::uintptr_t FramebufferAddress();
bool IsFramebuffer(std::uintptr_t address);
void Target(unsigned index);

enum class Action { kNone, kPrevious, kNext, kRun, kStop };
Action Process(const std::uint16_t *pipe1, const std::uint16_t *pipe2,
			   const uai::ai::ui::TouchPoint &touch, std::uint32_t first, std::uint32_t second);

} // namespace experiment::hwtest::display_log
