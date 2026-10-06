#pragma once

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

namespace uai::ai::mini {

/* Event flag bit set by the initialize task once NOR, PSRAM, and the model
 * data are reachable. The inference task waits for it before touching NPU. */
inline constexpr UINT kExternalMemoryReady = 0x01U;

/* Pipe2 frames waiting for the inference task. The channel keeps the newest
 * frames and returns dropped ones to the memory manager. */
inline constexpr std::size_t kFrameQueueDepth = 2U;
/* Inference results waiting for the camera task. Only the newest is shown. */
inline constexpr std::size_t kResultQueueDepth = 2U;

/* Hold the last boxes this long without a newer result before clearing them,
 * so a stalled model does not leave stale boxes on a live camera image. */
inline constexpr std::uint32_t kResultHoldMs = 3000U;

inline constexpr PRI kInitializeTaskPriority = 5;
inline constexpr PRI kCameraTaskPriority = 5;
inline constexpr PRI kInferenceTaskPriority = 6;

inline constexpr SZ kInitializeTaskStackSize = 32U * 1024U;
inline constexpr SZ kCameraTaskStackSize = 32U * 1024U;
inline constexpr SZ kInferenceTaskStackSize = 32U * 1024U;

} // namespace uai::ai::mini
