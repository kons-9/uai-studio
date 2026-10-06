#include "memory_manager/memory_manager.hpp"

#include <gtest/gtest.h>
#include <cstdint>

#include "middleware/memory/generated/static_memory_layout/key.hpp"
#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/pipeline/frame_types.hpp"

namespace uai::ai::static_memory_layout {

const Region Region::GetRegionFromKey(Key key)
{
    const auto address = 0x91000000U +
                         static_cast<std::uintptr_t>(key) * 0x100000U;
    return {reinterpret_cast<const std::uint8_t *>(address),
            reinterpret_cast<const std::uint8_t *>(address + 0x100000U)};
}

} // namespace uai::ai::static_memory_layout

using namespace uai::ai;

namespace {

class MemoryManagerTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(memory.Initialize().Ok()); }
    memory_manager::MemoryManager memory{};
};

TEST_F(MemoryManagerTest, DisplayHandoff)
{
    pipeline::DisplayBuffer first{}, second{}, unavailable{};
    ASSERT_TRUE(memory.AcquireDisplayBuffer(&first).Ok());
    ASSERT_TRUE(memory.CommitDisplayBuffer(first).Ok());
    ASSERT_TRUE(memory.AcquireDisplayBuffer(&second).Ok());
    EXPECT_NE(first.buffer.address, second.buffer.address);
    EXPECT_FALSE(memory.CommitDisplayBuffer(second).Ok());
    ASSERT_TRUE(memory.ReleaseDisplayBuffer(second).Ok());
    ASSERT_TRUE(memory.CompleteDisplayHandoff().Ok());
    ASSERT_TRUE(memory.AcquireDisplayBuffer(&second).Ok());
    ASSERT_TRUE(memory.CommitDisplayBuffer(second).Ok());
    EXPECT_FALSE(memory.AcquireDisplayBuffer(&unavailable).Ok());
    ASSERT_TRUE(memory.CompleteDisplayHandoff().Ok());
    ASSERT_TRUE(memory.AcquireDisplayBuffer(&unavailable).Ok());
    EXPECT_EQ(unavailable.buffer.address, first.buffer.address);
    EXPECT_TRUE(memory.ReleaseDisplayBuffer(unavailable).Ok());
}

TEST_F(MemoryManagerTest, InferenceLease)
{
    memory_allocator::Buffer capture_buffer{};
    ASSERT_TRUE(memory.CaptureBuffer(0U, &capture_buffer).Ok());
    pipeline::CaptureFrame capture{};
    ASSERT_TRUE(memory.ImportCompletedCapture(capture_buffer.address, &capture).Ok());
    pipeline::InferenceFrame first{}, second{};
    ASSERT_TRUE(memory.AcquireInferenceBuffer(capture, &first).Ok());
    EXPECT_EQ(first.output_count, 2U);
    EXPECT_LT(first.outputs[0].address, first.outputs[1].address);
    ASSERT_TRUE(memory.ClaimInferenceBuffer(first).Ok());
    EXPECT_FALSE(memory.ClaimInferenceBuffer(first).Ok());
    ASSERT_TRUE(memory.ReleaseInferenceBuffer(first).Ok());

    ASSERT_TRUE(memory.AcquireInferenceBuffer(capture, &second).Ok());
    EXPECT_EQ(second.buffer.address, first.buffer.address);
    EXPECT_NE(second.lease_token, first.lease_token);
    EXPECT_FALSE(memory.ClaimInferenceBuffer(first).Ok());
    EXPECT_FALSE(memory.ReleaseInferenceBuffer(first).Ok());
    ASSERT_TRUE(memory.ReleaseInferenceBuffer(second).Ok());

    pipeline::CaptureFrame newer{};
    ASSERT_TRUE(memory.ImportCompletedCapture(capture_buffer.address, &newer).Ok());
    EXPECT_FALSE(memory.ValidateCaptureFrame(capture).Ok());
    EXPECT_TRUE(memory.ValidateCaptureFrame(newer).Ok());
}

TEST_F(MemoryManagerTest, Pipe2Handoff)
{
    memory_allocator::Buffer buffer{};
    ASSERT_TRUE(memory.InferenceBuffer(0U, &buffer).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(buffer.address, 42U).Ok());
    EXPECT_FALSE(memory.IsInferenceBufferFree(buffer.address));
    pipeline::InferenceFrame first{}, second{};
    EXPECT_FALSE(memory.ImportCompletedInference(buffer.address, 41U, &first).Ok());
    ASSERT_TRUE(memory.ImportCompletedInference(buffer.address, 42U, &first).Ok());
    EXPECT_TRUE(first.from_pipe2);
    EXPECT_EQ(first.capture_sequence, 42U);
    ASSERT_TRUE(memory.ClaimInferenceBuffer(first).Ok());
    EXPECT_FALSE(memory.DropCompletedInference(buffer.address, 42U).Ok());
    ASSERT_TRUE(memory.ReleaseInferenceBuffer(first).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(buffer.address, 42U).Ok());
    ASSERT_TRUE(memory.ImportCompletedInference(buffer.address, 42U, &second).Ok());
    EXPECT_NE(second.lease_token, first.lease_token);
    EXPECT_FALSE(memory.ReleaseInferenceBuffer(first).Ok());
    ASSERT_TRUE(memory.DropCompletedInference(buffer.address, 42U).Ok());
    EXPECT_TRUE(memory.IsInferenceBufferFree(buffer.address));
}

} // namespace