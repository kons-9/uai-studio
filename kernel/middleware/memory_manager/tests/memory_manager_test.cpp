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
    buffer::Buffer capture_buffer{};
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
    buffer::Buffer buffer{};
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

TEST_F(MemoryManagerTest, StaleLeaseIsRejectedAfterSlotReuse)
{
    buffer::Buffer capture_buffer{};
    ASSERT_TRUE(memory.CaptureBuffer(0U, &capture_buffer).Ok());
    pipeline::CaptureFrame capture{};
    ASSERT_TRUE(memory.ImportCompletedCapture(capture_buffer.address, &capture).Ok());

    pipeline::InferenceFrame held[memory_manager::kInferenceBufferCount]{};
    for (auto &frame : held) {
        ASSERT_TRUE(memory.AcquireInferenceBuffer(capture, &frame).Ok());
        EXPECT_NE(frame.lease_token, 0U);
    }
    pipeline::InferenceFrame extra{};
    EXPECT_EQ(memory.AcquireInferenceBuffer(capture, &extra).Code(),
              common::ErrorCode::kNoBuffer);

    ASSERT_TRUE(memory.ReleaseInferenceBuffer(held[0]).Ok());
    EXPECT_EQ(memory.ReleaseInferenceBuffer(held[0]).Code(),
              common::ErrorCode::kOwnership);
    ASSERT_TRUE(memory.AcquireInferenceBuffer(capture, &extra).Ok());
    EXPECT_EQ(extra.buffer.address, held[0].buffer.address);
    EXPECT_NE(extra.lease_token, held[0].lease_token);
    EXPECT_EQ(memory.ClaimInferenceBuffer(held[0]).Code(),
              common::ErrorCode::kOwnership);

    pipeline::InferenceFrame forged = extra;
    forged.lease_token = 0U;
    EXPECT_EQ(memory.ClaimInferenceBuffer(forged).Code(),
              common::ErrorCode::kOwnership);
    forged = extra;
    forged.buffer.region = buffer::Region::kDisplay;
    EXPECT_EQ(memory.ClaimInferenceBuffer(forged).Code(),
              common::ErrorCode::kInvalidArgument);
    forged = extra;
    forged.buffer.index = static_cast<std::uint8_t>(
        memory_manager::kInferenceBufferCount);
    EXPECT_EQ(memory.ClaimInferenceBuffer(forged).Code(),
              common::ErrorCode::kInvalidArgument);
    EXPECT_TRUE(memory.ClaimInferenceBuffer(extra).Ok());
}

TEST_F(MemoryManagerTest, RejectsUseBeforeInitializeAndDoubleInitialize)
{
    memory_manager::MemoryManager fresh{};
    pipeline::DisplayBuffer display{};
    EXPECT_EQ(fresh.AcquireDisplayBuffer(&display).Code(),
              common::ErrorCode::kNotInitialized);
    EXPECT_FALSE(fresh.IsInferenceBufferFree(0U));
    EXPECT_EQ(memory.Initialize().Code(),
              common::ErrorCode::kAlreadyInitialized);
}

} // namespace
