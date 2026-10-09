#pragma once

#include <cstdint>

extern "C" {
extern std::uint16_t experiment_hwtest_display_framebuffer[800U * 480U];
}

namespace experiment::hwtest::display_log {

bool Initialize();
bool Ready();
bool SelfTest();
void Write(const char *text);

} // namespace experiment::hwtest::display_log
