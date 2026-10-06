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
std::deque<ER> send_failures;
std::deque<INT> receive_failures;
std::size_t send_calls = 0U;

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
    ++send_calls;
    if (!send_failures.empty()) {
        const ER error = send_failures.front();
        send_failures.pop_front();
        return error;
    }
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
    if (!receive_failures.empty()) {
        const INT error = receive_failures.front();
        receive_failures.pop_front();
        return error;
    }
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
        send_failures.clear();
        receive_failures.clear();
        send_calls = 0U;
        ASSERT_TRUE(memory.Initialize().Ok());
    }

    memory_manager::MemoryManager memory{};
};

TEST_F(FrameChannelsTest, FullFrameQueueReleasesOldestLease)
{
    capacity_override = 1;
    InferenceFrameChannel channel(memory);
    ASSERT_GT(channel.Create(), 0);

    buffer::Buffer first_buffer{}, second_buffer{};
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

TEST_F(FrameChannelsTest, ResultRetriesOnlyOnceOnOverflow)
{
    capacity_override = 1;
    InferenceResultChannel channel;
    inference::BoxSet active{};
    EXPECT_EQ(channel.PublishLatest(active).Code(),
              common::ErrorCode::kNotInitialized);
    EXPECT_EQ(send_calls, 0U);
    ASSERT_GT(channel.Create(), 0);
    active.person_valid = true;
    active.model_sequence = 1U;
    ASSERT_TRUE(channel.PublishLatest(active).Ok());

    active.model_sequence = 2U;
    send_failures.push_back(E_TMOUT);
    send_failures.push_back(E_TMOUT);
    const auto calls_before = send_calls;
    EXPECT_EQ(channel.PublishLatest(active).Code(),
              common::ErrorCode::kBufferOverflow);
    EXPECT_EQ(send_calls - calls_before, 2U);
    EXPECT_FALSE(channel.DrainLatest(nullptr));
    inference::BoxSet received{};
    EXPECT_FALSE(channel.DrainLatest(&received));

    active.model_sequence = 3U;
    ASSERT_TRUE(channel.PublishLatest(active).Ok());
    send_failures.push_back(static_cast<ER>(-42));
    EXPECT_EQ(channel.PublishLatest(active).Code(), common::ErrorCode::kHardware);
    EXPECT_EQ(buffers.front().messages.size(), 1U);
    ASSERT_TRUE(channel.DrainLatest(&received));
    EXPECT_EQ(received.model_sequence, 3U);
}

TEST_F(FrameChannelsTest, ConsecutiveFullResultsKeepNewest)
{
    capacity_override = 1;
    InferenceResultChannel channel;
    ASSERT_GT(channel.Create(), 0);
    inference::BoxSet result{};
    result.person_valid = true;
    for (std::uint32_t sequence = 1U; sequence <= 3U; ++sequence) {
        result.model_sequence = sequence;
        ASSERT_TRUE(channel.PublishLatest(result).Ok());
    }
    EXPECT_EQ(send_calls, 5U);
    inference::BoxSet active{};
    ASSERT_TRUE(channel.DrainLatest(&active));
    EXPECT_EQ(active.model_sequence, 3U);
}

TEST_F(FrameChannelsTest, DrainLatestKeepsLastValidResult)
{
    InferenceResultChannel channel;
    ASSERT_GT(channel.Create(), 0);
    inference::BoxSet active{};
    active.model_sequence = 99U;
    inference::BoxSet result{};
    result.model_sequence = 1U;
    ASSERT_TRUE(channel.PublishLatest(result).Ok());
    EXPECT_FALSE(channel.DrainLatest(&active));
    EXPECT_EQ(active.model_sequence, 99U);

    result.person_valid = true;
    result.model_sequence = 2U;
    ASSERT_TRUE(channel.PublishLatest(result).Ok());
    result.person_valid = false;
    result.model_sequence = 3U;
    ASSERT_TRUE(channel.PublishLatest(result).Ok());
    result.face_valid = true;
    result.model_sequence = 4U;
    ASSERT_TRUE(channel.PublishLatest(result).Ok());
    ASSERT_TRUE(channel.DrainLatest(&active));
    EXPECT_EQ(active.model_sequence, 4U);
    EXPECT_FALSE(channel.DrainLatest(&active));
    EXPECT_EQ(active.model_sequence, 4U);
}

TEST_F(FrameChannelsTest, FrameSendFailureReturnsTheInputLease)
{
    InferenceFrameChannel channel(memory);
    ASSERT_GT(channel.Create(), 0);
    buffer::Buffer buffer{};
    ASSERT_TRUE(memory.InferenceBuffer(0U, &buffer).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(buffer.address, 1U).Ok());
    pipeline::InferenceFrame frame{};
    ASSERT_TRUE(memory.ImportCompletedInference(buffer.address, 1U, &frame).Ok());
    send_failures.push_back(static_cast<ER>(-42));
    receive_failures.push_back(static_cast<INT>(-42));

    channel.Send(frame);
    EXPECT_EQ(send_calls, 1U);
    EXPECT_TRUE(memory.IsInferenceBufferFree(buffer.address));
    EXPECT_TRUE(buffers.front().messages.empty());
}

TEST_F(FrameChannelsTest, FrameNonOverflowFailureReleasesOldestThenRetries)
{
    capacity_override = 1;
    InferenceFrameChannel channel(memory);
    ASSERT_GT(channel.Create(), 0);
    buffer::Buffer first_buffer{}, second_buffer{};
    ASSERT_TRUE(memory.InferenceBuffer(0U, &first_buffer).Ok());
    ASSERT_TRUE(memory.InferenceBuffer(1U, &second_buffer).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(first_buffer.address, 1U).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(second_buffer.address, 2U).Ok());
    pipeline::InferenceFrame first{}, second{}, received{};
    ASSERT_TRUE(memory.ImportCompletedInference(first_buffer.address, 1U, &first).Ok());
    ASSERT_TRUE(memory.ImportCompletedInference(second_buffer.address, 2U, &second).Ok());
    channel.Send(first);
    send_failures.push_back(static_cast<ER>(-42));

    channel.Send(second);
    EXPECT_EQ(send_calls, 3U);
    EXPECT_TRUE(memory.IsInferenceBufferFree(first_buffer.address));
    EXPECT_FALSE(memory.IsInferenceBufferFree(second_buffer.address));
    ASSERT_TRUE(channel.Receive(&received));
    EXPECT_EQ(received.lease_token, second.lease_token);
    EXPECT_TRUE(memory.ReleaseInferenceBuffer(received).Ok());
}

TEST_F(FrameChannelsTest, FrameReceiveFailureRetainsQueuedLease)
{
    capacity_override = 1;
    InferenceFrameChannel channel(memory);
    ASSERT_GT(channel.Create(), 0);
    buffer::Buffer first_buffer{}, second_buffer{};
    ASSERT_TRUE(memory.InferenceBuffer(0U, &first_buffer).Ok());
    ASSERT_TRUE(memory.InferenceBuffer(1U, &second_buffer).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(first_buffer.address, 1U).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(second_buffer.address, 2U).Ok());
    pipeline::InferenceFrame first{}, second{}, received{};
    ASSERT_TRUE(memory.ImportCompletedInference(first_buffer.address, 1U, &first).Ok());
    ASSERT_TRUE(memory.ImportCompletedInference(second_buffer.address, 2U, &second).Ok());
    channel.Send(first);
    receive_failures.push_back(static_cast<INT>(-42));
    channel.Send(second);

    EXPECT_EQ(send_calls, 2U);
    EXPECT_FALSE(memory.IsInferenceBufferFree(first_buffer.address));
    EXPECT_TRUE(memory.IsInferenceBufferFree(second_buffer.address));
    ASSERT_TRUE(channel.Receive(&received));
    EXPECT_EQ(received.lease_token, first.lease_token);
    EXPECT_TRUE(memory.ReleaseInferenceBuffer(received).Ok());
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