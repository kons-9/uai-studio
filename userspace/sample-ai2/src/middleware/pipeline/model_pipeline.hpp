#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::pipeline {

/* Stage は各タスクの内部状態。タスク間のキューには stage ID ではなく、
 * 次の実行コンテキストとフレームの lease を渡す。 */
enum class ExecutionContext : std::uint8_t {
    kCpuInputTask, kNpuTask, kCpuPostprocessTask,
};

struct Handoff {
    ExecutionContext from = ExecutionContext::kNpuTask;
    ExecutionContext to = ExecutionContext::kCpuInputTask;
    std::uint64_t request_id = 0U;
    std::uint64_t lease_token = 0U;
};

constexpr bool ValidHandoff(const Handoff &handoff)
{
    if (handoff.request_id == 0U) {
        return false;
    }
    if (handoff.from == ExecutionContext::kNpuTask &&
        handoff.to == ExecutionContext::kCpuInputTask) {
        return handoff.lease_token == 0U; // フレーム取得前
    }
    if (handoff.lease_token == 0U) {
        return false;
    }
    return (handoff.from == ExecutionContext::kCpuInputTask &&
            handoff.to == ExecutionContext::kNpuTask) ||
           (handoff.from == ExecutionContext::kNpuTask &&
            handoff.to == ExecutionContext::kCpuPostprocessTask);
}

constexpr bool IsPreparedFor(const Handoff &handoff,
                             std::uint64_t request_id,
                             std::uint64_t lease_token)
{
    return ValidHandoff(handoff) &&
           handoff.from == ExecutionContext::kCpuInputTask &&
           handoff.to == ExecutionContext::kNpuTask &&
           handoff.request_id == request_id &&
           handoff.lease_token == lease_token;
}

constexpr bool IsCompletionFor(const Handoff &handoff,
                               std::uint64_t lease_token)
{
    return ValidHandoff(handoff) &&
           handoff.from == ExecutionContext::kNpuTask &&
           handoff.to == ExecutionContext::kCpuPostprocessTask &&
           handoff.lease_token == lease_token;
}

/* 各タスク内の処理の種類。数値はトレース形式と対応するため変更しない。
 * kOutputCache 以降は旧トレース用の ID であり、現在の Plan には含まれない。
 * NPU 完了後のデコードは別タスクが実行する。 */
enum class Stage : std::uint8_t {
    kCopy, kResize, kLetterbox, kInputCache, kSubmit, kIrqWait,
    kEpochContinue, kOutputCache, kDecode, kConvert, kFinalize,
};
/* その処理を担当する場所の説明。現行 Plan の stage は全て推論タスク上の
 * 関数呼び出しであり、NPU 内部や IRQ ハンドラそのものを表していない。 */
enum class Location : std::uint8_t {
    kTaskCpu, kNpuHardware, kNpuIrq,
};
/* 「次のフレームの CPU 前処理として、前フレームの NPU 実行中に先読み可能か」。
 * stage 同士を並列に走らせる指示ではない。kIrqWait 中に先読みが起こる場合も
 * kIrqWait 自体を先読みするわけではないため、kSerial とする。 */
enum class Overlap : std::uint8_t {
    kSerial, kMayOverlap,
};
/* 1 stage の静的な説明。id がディスパッチに使われ、location/overlap は
 * Valid() で契約を検査する。name/processing は説明用で、関数ポインタでも
 * 処理データでもない。実行時の進捗やバッファ所有権は保持しない。 */
struct Descriptor {
    Stage id;
    const char *name;
    Location location;
    Overlap overlap;
    const char *processing;
};
/* 実行順の静的な定義。stages は不変の配列を参照し、count は要素数。
 * 配列の所有・確保は行わず、各実行の状態はディスパッチャが保持する。 */
struct Plan {
    const Descriptor *stages = nullptr;
    std::size_t count = 0U;
};

inline constexpr Descriptor kNpuStages[] = {
    {Stage::kInputCache, "input_cache", Location::kTaskCpu,
     Overlap::kSerial, "synchronize input cache with NPU"},
    {Stage::kSubmit, "submit", Location::kTaskCpu,
     Overlap::kSerial, "start asynchronous inference"},
    {Stage::kIrqWait, "irq_wait", Location::kTaskCpu,
     Overlap::kSerial, "poll status, optionally prefetch and wait for IRQ"},
    {Stage::kEpochContinue, "epoch_continue", Location::kTaskCpu,
     Overlap::kSerial, "continue only if the run is not complete"},
};
inline constexpr Descriptor kResizedStages[] = {
    {Stage::kCopy, "copy", Location::kTaskCpu,
     Overlap::kMayOverlap, "copy input to private scratch"},
    {Stage::kResize, "resize", Location::kTaskCpu,
     Overlap::kMayOverlap, "resize in a different slot from active NPU"},
    {Stage::kLetterbox, "letterbox", Location::kTaskCpu,
     Overlap::kMayOverlap, "fill private input padding"},
};
/* NPU プロトコルとモデル CPU 入力処理は別々の task 内で実行する計画。
 * task 間には Plan ではなく Handoff とフレームを渡す。 */
inline constexpr Plan kNpuProtocol{kNpuStages,
                                   sizeof(kNpuStages) / sizeof(kNpuStages[0])};
inline constexpr Plan kResized{kResizedStages,
                               sizeof(kResizedStages) / sizeof(kResizedStages[0])};

constexpr bool IsInputPreparation(Stage stage)
{
    return stage == Stage::kCopy || stage == Stage::kResize ||
           stage == Stage::kLetterbox;
}

/* 各コンテキスト内で実行できる順序・担当・先読み契約だけを許可する。
 * epoch_continue は未完了の場合だけ戻って実行する条件付きの処理で、
 * 毎回必ず末尾に実行する段階ではない。 */
constexpr bool Valid(const Plan &plan)
{
    if (plan.stages == nullptr || (plan.count != 3U && plan.count != 4U)) {
        return false;
    }
    if (plan.count == 3U) {
        const Stage prefix[] = {Stage::kCopy, Stage::kResize,
                                Stage::kLetterbox};
        for (std::size_t i = 0U; i < 3U; ++i) {
            if (plan.stages[i].id != prefix[i] ||
                plan.stages[i].location != Location::kTaskCpu ||
                plan.stages[i].overlap != Overlap::kMayOverlap) {
                return false;
            }
        }
        return true;
    }
    const Stage protocol[] = {Stage::kInputCache, Stage::kSubmit,
                              Stage::kIrqWait, Stage::kEpochContinue};
    for (std::size_t i = 0U; i < 4U; ++i) {
        if (plan.stages[i].id != protocol[i] ||
            plan.stages[i].location != Location::kTaskCpu ||
            plan.stages[i].overlap != Overlap::kSerial) {
            return false;
        }
    }
    return true;
}
static_assert(Valid(kNpuProtocol) && Valid(kResized));

} // namespace uai::ai::pipeline
