#include "buffer_layout/buffer_layout.hpp"
#include "buffer_layout/lease_pool.hpp"
#include "buffer_layout/region_guard.hpp"
#include "driver/camera_driver/dcmipp_resize.hpp"
#include "image_resizer/image_resizer.hpp"
#include "pipeline/model_pipeline.hpp"

#include <cassert>
#include <cstdint>

int main()
{
    using namespace uai::ai;
    constexpr buffer_layout::Config config{
        800U, 480U, 2U, 480U, 480U, 3U, 32U,
        {8192U, 64800U, 16200U, 6144U}, 480U, 288U, 16U};
    static_assert(config.frame_bytes() == 768000U);
    static_assert(config.inference_outputs_offset() == 691200U);
    static_assert(config.inference_buffer_bytes() < 0x100000U);
    using pipeline::ExecutionContext;
    constexpr pipeline::Handoff request{
        ExecutionContext::kNpuTask, ExecutionContext::kCpuInputTask, 7U, 0U};
    constexpr pipeline::Handoff prepared{
        ExecutionContext::kCpuInputTask, ExecutionContext::kNpuTask, 7U, 42U};
    constexpr pipeline::Handoff completed{
        ExecutionContext::kNpuTask, ExecutionContext::kCpuPostprocessTask,
        7U, 42U};
    static_assert(pipeline::ValidHandoff(request));
    static_assert(pipeline::ValidHandoff(prepared));
    static_assert(pipeline::ValidHandoff(completed));
    static_assert(pipeline::IsPreparedFor(prepared, 7U, 42U));
    static_assert(!pipeline::IsPreparedFor(prepared, 8U, 42U));
    static_assert(!pipeline::IsPreparedFor(prepared, 7U, 43U));
    static_assert(!pipeline::IsPreparedFor(completed, 7U, 42U));
    static_assert(pipeline::IsCompletionFor(completed, 42U));
    static_assert(!pipeline::IsCompletionFor(prepared, 42U));
    static_assert(!pipeline::IsCompletionFor(completed, 43U));
    static_assert(!pipeline::ValidHandoff({ExecutionContext::kNpuTask,
        ExecutionContext::kCpuPostprocessTask, 7U, 0U}));
    static_assert(!pipeline::ValidHandoff({ExecutionContext::kCpuInputTask,
        ExecutionContext::kCpuPostprocessTask, 7U, 42U}));
    static_assert(!pipeline::ValidHandoff({ExecutionContext::kCpuInputTask,
        ExecutionContext::kNpuTask, 0U, 42U}));
    static_assert(pipeline::Valid(pipeline::kNpuProtocol));
    static_assert(pipeline::Valid(pipeline::kResized));
    constexpr pipeline::Descriptor bad_stages[] = {
        pipeline::kNpuStages[1], pipeline::kNpuStages[0],
        pipeline::kNpuStages[2], pipeline::kNpuStages[3]};
    static_assert(!pipeline::Valid({bad_stages, 4U}));
    constexpr pipeline::Descriptor bad_overlap[] = {
        pipeline::kNpuStages[0], pipeline::kNpuStages[1],
        {pipeline::Stage::kIrqWait, "irq_wait", pipeline::Location::kTaskCpu,
         pipeline::Overlap::kMayOverlap, "unsafe wait"},
        pipeline::kNpuStages[3]};
    static_assert(!pipeline::Valid({bad_overlap, 4U}));
    constexpr pipeline::Descriptor bad_location[] = {
        pipeline::kResizedStages[0],
        {pipeline::Stage::kResize, "resize", pipeline::Location::kNpuHardware,
         pipeline::Overlap::kMayOverlap, "incorrect hardware attribution"},
        pipeline::kResizedStages[2]};
    static_assert(!pipeline::Valid({bad_location, 3U}));
    constexpr pipeline::Descriptor bad_runtime_location[] = {
        pipeline::kNpuStages[0], pipeline::kNpuStages[1],
        {pipeline::Stage::kIrqWait, "irq_wait", pipeline::Location::kNpuIrq,
         pipeline::Overlap::kSerial, "task-side polling is not IRQ work"},
        pipeline::kNpuStages[3]};
    static_assert(!pipeline::Valid({bad_runtime_location, 4U}));
    constexpr pipeline::Descriptor mixed_contexts[] = {
        pipeline::kResizedStages[0], pipeline::kResizedStages[1],
        pipeline::kResizedStages[2], pipeline::kNpuStages[0]};
    static_assert(!pipeline::Valid({mixed_contexts, 4U}));

    using buffer_layout::Range;
    static_assert(buffer_layout::Contains({0x1000U, 0x200U},
                                          {0x1100U, 0x100U}));
    static_assert(!buffer_layout::Contains({0x1000U, 0x200U},
                                           {0x11F0U, 0x20U}));
    static_assert(!buffer_layout::Overlaps({0x1000U, 0x20U},
                                           {0x1020U, 0x20U}));
    const Range regions[] = {{0x1000U, 0x20U}, {0x1020U, 0x20U}};
    assert(buffer_layout::Disjoint(regions, 2U));
    const Range overlap[] = {{0x1000U, 0x21U}, {0x1020U, 0x20U}};
    assert(!buffer_layout::Disjoint(overlap, 2U));

    alignas(32) std::uint32_t guard[8]{};
    buffer_layout::ArmGuard(reinterpret_cast<std::uintptr_t>(guard));
    assert(buffer_layout::GuardIntact(reinterpret_cast<std::uintptr_t>(guard)));
    guard[7] = 0U;
    assert(!buffer_layout::GuardIntact(reinterpret_cast<std::uintptr_t>(guard)));

    buffer_layout::LeasePool<5U> leases{};
    assert(leases.Reserve(0U, 42U) && leases.Reserve(1U, 43U));
    assert(leases.Reserve(2U, 44U) && leases.Reserve(3U, 45U) &&
           leases.Reserve(4U, 46U));
    assert(!leases.Reserve(0U, 47U));
    const std::uint64_t old_token = leases.TokenAt(0U);
    assert(leases.Claim(0U, 42U, old_token));
    assert(!leases.Claim(0U, 42U, old_token));
    assert(!leases.DropReady(0U, 42U));
    assert(leases.Release(0U, 42U, old_token));
    assert(!leases.Release(0U, 42U, old_token));
    assert(leases.Reserve(0U, 42U));  // Same sequence after wraparound.
    assert(!leases.Claim(0U, 42U, old_token));
    assert(leases.DropReady(0U, 42U));
    assert(!leases.Reserve(5U, 44U));

    camera::dcmipp_resize::Selection selection{};
    assert(camera::dcmipp_resize::Select(2592U, 1944U, 128U, 128U,
                                         &selection).Ok());
    assert(selection.decimation == 4U);
    assert(selection.input_width == 648U);
    assert(!camera::dcmipp_resize::Select(0U, 100U, 128U, 128U,
                                          &selection).Ok());
    assert(!camera::dcmipp_resize::Select(50000U, 50000U, 128U, 128U,
                                          &selection).Ok());

    const std::uint16_t pixels[2] = {0xF800U, 0x07E0U};
    std::uint8_t output[6]{};
    assert(image_resizer::ResizeRgb565ToRgb888(
        {pixels, 2U, 1U, 2U}, 0U, 0U, 2U, 1U,
        {output, 2U, 1U, 6U}).Ok());
    assert(output[0] == 255U && output[1] == 0U && output[2] == 0U);
    assert(output[3] == 0U && output[4] == 255U && output[5] == 0U);
    return 0;
}
