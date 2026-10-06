#include "task/pipeline_task.hpp"

#include <cstring>
#include <deque>
#include <vector>

#include <gtest/gtest.h>

#include "memory_manager/memory_manager.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"
#include "middleware/memory/static_memory_layout.hpp"

namespace uai::ai::static_memory_layout {

const Region Region::GetRegionFromKey(Key key)
{
    const auto address = 0x91000000U +
                         static_cast<std::uintptr_t>(key) * 0x100000U;
    return {reinterpret_cast<const std::uint8_t *>(address),
            reinterpret_cast<const std::uint8_t *>(address + 0x100000U)};
}

} // namespace uai::ai::static_memory_layout

namespace {

struct MessageBuffer {
    SZ max_size;
    SZ capacity;
    std::deque<std::vector<std::uint8_t>> messages;
};

std::vector<MessageBuffer> buffers;
SZ capacity_override = 0;

} // namespace

ID tk_cre_mbf(const T_CMBF *config)
{
    if (!config || !(config->mbfatr & TA_USERBUF) || !config->bufptr ||
        config->maxmsz == 0) return -1;
    const SZ slot_size = sizeof(INT) +
                         (config->maxmsz + sizeof(INT) - 1U) / sizeof(INT) *
                             sizeof(INT);
    buffers.push_back({config->maxmsz,
                       capacity_override ? capacity_override :
                           config->bufsz / slot_size, {}});
    return static_cast<ID>(buffers.size());
}

ER tk_snd_mbf(ID queue, const void *message, SZ size, TMO)
{
    auto &buffer = buffers.at(static_cast<std::size_t>(queue - 1));
    if (size > buffer.max_size || buffer.messages.size() >= buffer.capacity) {
        return E_TMOUT;
    }
    const auto *data = static_cast<const std::uint8_t *>(message);
    buffer.messages.emplace_back(data, data + size);
    return E_OK;
}

INT tk_rcv_mbf(ID queue, void *message, TMO)
{
    auto &buffer = buffers.at(static_cast<std::size_t>(queue - 1));
    if (buffer.messages.empty()) return -1;
    const auto data = buffer.messages.front();
    std::memcpy(message, data.data(), data.size());
    buffer.messages.pop_front();
    return static_cast<INT>(data.size());
}

namespace uai::ai::task {

namespace {

class FrameChannelsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        buffers.clear();
        capacity_override = 0;
        ASSERT_TRUE(memory.Initialize().Ok());
    }

    memory_manager::MemoryManager memory{};
};

TEST_F(FrameChannelsTest, FullFrameQueueReleasesOldestLease)
{
    capacity_override = 1;
    InferenceFrameChannel channel(memory);
    ASSERT_GT(channel.Create(), 0);

    memory_allocator::Buffer first_buffer{}, second_buffer{};
    ASSERT_TRUE(memory.InferenceBuffer(0U, &first_buffer).Ok());
    ASSERT_TRUE(memory.InferenceBuffer(1U, &second_buffer).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(first_buffer.address, 1U).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(second_buffer.address, 2U).Ok());
    pipeline::InferenceFrame first{}, second{}, received{};
    ASSERT_TRUE(memory.ImportCompletedInference(first_buffer.address, 1U, &first).Ok());
    ASSERT_TRUE(memory.ImportCompletedInference(second_buffer.address, 2U, &second).Ok());

    channel.Send(first);
    channel.Send(second);
    EXPECT_TRUE(memory.IsInferenceBufferFree(first_buffer.address));
    EXPECT_FALSE(memory.IsInferenceBufferFree(second_buffer.address));
    ASSERT_TRUE(channel.Receive(&received));
    EXPECT_EQ(received.lease_token, second.lease_token);
    EXPECT_FALSE(channel.Receive(&received));
    EXPECT_TRUE(memory.ReleaseInferenceBuffer(received).Ok());
}

TEST_F(FrameChannelsTest, ResultQueueKeepsNewestAndDrainIgnoresInvalidResult)
{
    InferenceResultChannel channel;
    ASSERT_GT(channel.Create(), 0);
    EXPECT_EQ(buffers.front().capacity, kResultQueueDepth);
    inference::BoxSet active{};
    active.model_sequence = 99U;
    EXPECT_FALSE(channel.DrainLatest(&active));

    for (std::uint32_t sequence = 1U; sequence <= 5U; ++sequence) {
        inference::BoxSet boxes{};
        boxes.model_sequence = sequence;
        boxes.person_valid = sequence != 5U;
        ASSERT_TRUE(channel.PublishLatest(boxes).Ok());
    }
    ASSERT_TRUE(channel.DrainLatest(&active));
    EXPECT_EQ(active.model_sequence, 4U);
    EXPECT_FALSE(channel.DrainLatest(&active));
    EXPECT_EQ(active.model_sequence, 4U);
}

TEST_F(FrameChannelsTest, ResultOwnsMaskAfterSourceIsReused)
{
    InferenceResultChannel channel;
    ASSERT_GT(channel.Create(), 0);

    std::uint8_t source[inference::kSegmentationMaskWidth *
                        inference::kSegmentationMaskHeight]{};
    source[0] = 1U;
    inference::BoxSet result{};
    result.segmentation_valid = true;
    result.segmentation.mask_width = inference::kSegmentationMaskWidth;
    result.segmentation.mask_height = inference::kSegmentationMaskHeight;
    ASSERT_TRUE(result.segmentation.mask.CopyFrom(source, sizeof(source)).Ok());
    ASSERT_TRUE(channel.PublishLatest(result).Ok());

    source[0] = 0U;
    result.segmentation.mask.bytes[0] = 0U;
    inference::BoxSet displayed{};
    ASSERT_TRUE(channel.DrainLatest(&displayed));
    EXPECT_EQ(displayed.segmentation.mask.data()[0], 1U);
    EXPECT_EQ(displayed.segmentation.mask.CopyFrom(source, sizeof(source) + 1U).Code(),
              common::ErrorCode::kBufferOverflow);
    EXPECT_EQ(displayed.segmentation.mask.data()[0], 1U);
}

} // namespace
} // namespace uai::ai::task