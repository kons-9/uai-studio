#include "task/pipeline_task.hpp"
#include "task/camera_render_state.hpp"
#include "task/pipeline_state.hpp"
#include "task/camera_render_wake.hpp"
#include <functional>

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
    const auto address = 0x91000000U + static_cast<std::uintptr_t>(key) * 0x100000U;
    return {
        reinterpret_cast<const std::uint8_t *>(address), reinterpret_cast<const std::uint8_t *>(address + 0x100000U)
    };
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
std::deque<INT> receive_results;
std::size_t send_calls = 0U;
std::vector<UINT> event_flags;
std::function<void()> wait_notification;

} // namespace

ID tk_cre_mbf(const T_CMBF *config)
{
    if (!config || !(config->mbfatr & TA_USERBUF) || !config->bufptr || config->maxmsz == 0)
        return -1;
    const SZ slot_size = sizeof(INT) + (config->maxmsz + sizeof(INT) - 1U) / sizeof(INT) * sizeof(INT);
    buffers.push_back({config->maxmsz, capacity_override ? capacity_override : config->bufsz / slot_size, {}});
    return static_cast<ID>(buffers.size());
}

ID tk_cre_flg(const T_CFLG *configuration)
{
    event_flags.push_back(configuration->iflgptn);
    return static_cast<ID>(event_flags.size());
}
ER tk_del_flg(ID)
{
    return E_OK;
}
ER tk_set_flg(
    ID flag,
    UINT pattern
)
{
    event_flags.at(static_cast<std::size_t>(flag - 1)) |= pattern;
    return E_OK;
}
ER tk_wai_flg(
    ID flag,
    UINT pattern,
    UINT mode,
    UINT *result,
    TMO
)
{
    if (wait_notification) {
        const auto notify = std::move(wait_notification);
        wait_notification = {};
        notify();
    }
    auto &bits = event_flags.at(static_cast<std::size_t>(flag - 1));
    *result = bits & pattern;
    if (*result == 0U)
        return E_TMOUT;
    if ((mode & TWF_BITCLR) != 0U)
        bits &= ~*result;
    return E_OK;
}

ER tk_snd_mbf(
    ID queue,
    const void *message,
    SZ size,
    TMO
)
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

INT tk_rcv_mbf(
    ID queue,
    void *message,
    TMO
)
{
    if (!receive_results.empty()) {
        const INT result = receive_results.front();
        receive_results.pop_front();
        if (result != E_OK)
            return result;
    }
    auto &buffer = buffers.at(static_cast<std::size_t>(queue - 1));
    if (buffer.messages.empty())
        return E_TMOUT;
    const auto data = buffer.messages.front();
    std::memcpy(message, data.data(), data.size());
    buffer.messages.pop_front();
    return static_cast<INT>(data.size());
}

namespace uai::ai::task {

namespace {

struct DiagnosticSnapshot {
    std::uint32_t dcmipp_error_count = 0U;
    std::uint32_t camera_error_count = 0U;
    std::uint32_t csi_error_count = 0U;
    std::uint32_t isp_error_count = 0U;
    std::uint32_t pipe1_timeout_count = 0U;
    std::uint32_t pipe2_timeout_count = 0U;
    std::uint32_t last_anomaly_tick = 0U;
    std::uint32_t recovery_count = 0U;
    std::uint32_t recovery_error_count = 0U;
};

TEST(
    CameraDiagnosticState,
    ReportsEachHealthChangeButNotUnchangedSnapshots
)
{
    std::uint32_t DiagnosticSnapshot::*const fields[] = {
        &DiagnosticSnapshot::dcmipp_error_count,
        &DiagnosticSnapshot::camera_error_count,
        &DiagnosticSnapshot::csi_error_count,
        &DiagnosticSnapshot::isp_error_count,
        &DiagnosticSnapshot::pipe1_timeout_count,
        &DiagnosticSnapshot::pipe2_timeout_count,
        &DiagnosticSnapshot::last_anomaly_tick,
    };
    for (const auto field : fields) {
        CameraDiagnosticState state(100U);
        DiagnosticSnapshot snapshot;
        EXPECT_FALSE(state.ShouldReport(snapshot, 100U));
        snapshot.*field = 1U;
        EXPECT_TRUE(state.ShouldReport(snapshot, 101U));
        state.MarkReported(snapshot, 101U);
        EXPECT_FALSE(state.ShouldReport(snapshot, 1101U));
    }
}

TEST(
    CameraRenderWake,
    CoalescesReasonsWithoutLosingWaitBoundaryNotification
)
{
    event_flags.clear();
    CameraRenderWake wake;
    ASSERT_TRUE(wake.Create().Ok());
    wake.Bind(CameraRenderWake::kCapture).Notify();
    wait_notification = [&] {
        wake.Bind(CameraRenderWake::kResult).Notify();
    };
    UINT reasons = 0U;
    ASSERT_TRUE(wake.Wait(UINT32_MAX, &reasons).Ok());
    EXPECT_EQ(reasons, CameraRenderWake::kCapture | CameraRenderWake::kResult);
    ASSERT_TRUE(wake.Wait(0U, &reasons).Ok());
    EXPECT_EQ(reasons, 0U);
    wake.Close();
    EXPECT_FALSE(wake.Wait(0U, &reasons).Ok());
}

TEST(
    CameraDiagnosticState,
    SuppressesRepeatedHardwareErrorsUntilOneSecond
)
{
    CameraDiagnosticState state(100U);
    DiagnosticSnapshot snapshot;
    snapshot.camera_error_count = 1U;
    ASSERT_TRUE(state.ShouldReport(snapshot, 101U));
    state.MarkReported(snapshot, 101U);
    snapshot.camera_error_count = 2U;
    EXPECT_FALSE(state.ShouldReport(snapshot, 1100U));
    EXPECT_TRUE(state.ShouldReport(snapshot, 1101U));
}

TEST(
    CameraDiagnosticState,
    PreservesImmediateTimeoutReportsBeforeHardwareErrors
)
{
    CameraDiagnosticState state(100U);
    DiagnosticSnapshot snapshot;
    snapshot.pipe1_timeout_count = 1U;
    state.MarkReported(snapshot, 101U);
    snapshot.pipe2_timeout_count = 1U;
    EXPECT_TRUE(state.ShouldReport(snapshot, 102U));
}

TEST(
    CameraDiagnosticState,
    ReportIntervalSurvivesClockWrap
)
{
    CameraDiagnosticState state(0U);
    DiagnosticSnapshot snapshot;
    snapshot.csi_error_count = 1U;
    state.MarkReported(snapshot, UINT32_MAX - 500U);
    snapshot.csi_error_count = 2U;
    EXPECT_FALSE(state.ShouldReport(snapshot, 498U));
    EXPECT_TRUE(state.ShouldReport(snapshot, 499U));
}

TEST(
    CameraDiagnosticState,
    ReadsClockOnlyWhenHealthSuppressionRequiresIt
)
{
    CameraDiagnosticState state(100U);
    DiagnosticSnapshot snapshot;
    std::size_t clock_reads = 0U;
    const auto clock = [&] {
        ++clock_reads;
        return 1100U;
    };
    EXPECT_FALSE(state.ShouldReport(snapshot, clock));
    snapshot.camera_error_count = 1U;
    EXPECT_TRUE(state.ShouldReport(snapshot, clock));
    EXPECT_EQ(clock_reads, 0U);
    state.MarkReported(snapshot, 100U);
    EXPECT_FALSE(state.ShouldReport(snapshot, clock));
    EXPECT_EQ(clock_reads, 0U);
    snapshot.camera_error_count = 2U;
    EXPECT_TRUE(state.ShouldReport(snapshot, clock));
    EXPECT_EQ(clock_reads, 1U);
}

TEST(
    CameraDiagnosticState,
    RecoveryReportsAreIndependentFromHealthSuppression
)
{
    CameraDiagnosticState state(100U);
    DiagnosticSnapshot snapshot;
    snapshot.camera_error_count = 1U;
    state.MarkReported(snapshot, 100U);
    EXPECT_FALSE(state.ShouldReportRecovery(snapshot));
    snapshot.recovery_count = 1U;
    EXPECT_TRUE(state.ShouldReportRecovery(snapshot));
    state.MarkRecoveryReported(snapshot);
    EXPECT_FALSE(state.ShouldReportRecovery(snapshot));
    snapshot.recovery_error_count = 1U;
    EXPECT_TRUE(state.ShouldReportRecovery(snapshot));
}

TEST(
    RenderSchedule,
    PreservesPhaseAndSelectsActualFrameSequence
)
{
    RenderSchedule schedule(100U);
    EXPECT_FALSE(schedule.TouchDue(99U));
    EXPECT_TRUE(schedule.TouchDue(100U));
    schedule.TouchPolled(105U);
    EXPECT_FALSE(schedule.TouchDue(109U));
    EXPECT_TRUE(schedule.TouchDue(110U));
    ASSERT_TRUE(schedule.ConfigureFrames(3U, 10U));
    EXPECT_FALSE(schedule.TakeInference(9U, 30U));
    EXPECT_EQ(schedule.TakeInference(10U, 30U).due_count, 1U);
    EXPECT_EQ(schedule.TakeInference(23U, 30U).due_count, 4U);
    EXPECT_EQ(schedule.Stats(RenderOperation::kSubmit).skipped, 3U);
    EXPECT_FALSE(schedule.ConfigureFrames(0U, 0U));
}

TEST(
    TimePeriod,
    SkipsMissedExecutionsWithoutMovingPhase
)
{
    common::TimePeriod period;
    EXPECT_FALSE(period.Configure(0U, 100U));
    ASSERT_TRUE(period.Configure(10U, 100U));
    EXPECT_FALSE(period.Take(99U));
    const auto decision = period.Take(135U);
    EXPECT_EQ(decision.due_count, 4U);
    EXPECT_EQ(decision.lateness, 35U);
    EXPECT_EQ(period.RemainingWait(135U), 5U);
    EXPECT_FALSE(period.Take(139U));
    EXPECT_EQ(period.Take(140U).due_count, 1U);
}

TEST(
    FramePeriod,
    HandlesSequenceJumpsAndWrap
)
{
    common::FramePeriod period;
    ASSERT_TRUE(period.Configure(3U, UINT32_MAX - 2U));
    EXPECT_EQ(period.Take(UINT32_MAX - 2U).due_count, 1U);
    EXPECT_FALSE(period.Take(UINT32_MAX));
    EXPECT_EQ(period.Take(0U).due_count, 1U);
    EXPECT_EQ(period.Take(10U).due_count, 3U);
    EXPECT_FALSE(period.Configure(0U, 0U));
}

TEST(
    RenderSchedule,
    DeadlinesSurviveClockWrap
)
{
    RenderSchedule schedule(UINT32_MAX - 5U);
    schedule.TouchPolled(UINT32_MAX - 5U);
    EXPECT_FALSE(schedule.TouchDue(UINT32_MAX));
    EXPECT_FALSE(schedule.TouchDue(3U));
    EXPECT_TRUE(schedule.TouchDue(4U));
    EXPECT_EQ(schedule.RemainingWait(3U, true), 1U);
}

TEST(
    PipelineSubmitState,
    CandidateOrderChangesOnlyAfterSuccessfulSubmission
)
{
    PipelineSubmitState<3U, 4U> state;
    EXPECT_EQ(state.CandidateIndex(0U), 0U);
    EXPECT_EQ(state.CandidateIndex(2U), 2U);
    EXPECT_EQ(state.CandidateIndex(0U), 0U);
    state.CommitSubmission(2U, 2U);
    EXPECT_EQ(state.submitted_count[0U], 0U);
    EXPECT_EQ(state.submitted_count[2U], 1U);
    EXPECT_EQ(state.CandidateIndex(0U), 3U);
    EXPECT_EQ(state.CandidateIndex(1U), 0U);
    state.CommitSubmission(1U, 3U);
    EXPECT_EQ(state.submitted_count[1U], 1U);
    EXPECT_EQ(state.CandidateIndex(0U), 0U);
}

TEST(
    PostprocessState,
    KeepsOtherModelsAndCopiesFreshResultsByValue
)
{
    PostprocessState state;
    inference::BoxSet person{};
    person.person_valid = true;
    person.person.count = 1U;
    person.person.boxes[0U].confidence = 0.75F;
    person.capture_sequence = 10U;
    state.Merge(person);
    person.person.count = 0U;
    inference::BoxSet face{};
    face.face_valid = true;
    face.face.count = 2U;
    face.capture_sequence = 11U;
    state.Merge(face);
    inference::BoxSet segmentation{};
    segmentation.segmentation_valid = true;
    segmentation.segmentation.mask_foreground_pixels = 23U;
    segmentation.capture_sequence = 12U;
    const auto &merged = state.Merge(segmentation);
    EXPECT_EQ(merged.person.count, 1U);
    EXPECT_FLOAT_EQ(merged.person.boxes[0U].confidence, 0.75F);
    EXPECT_EQ(merged.face.count, 2U);
    EXPECT_EQ(merged.segmentation.mask_foreground_pixels, 23U);
    EXPECT_TRUE(merged.person_valid && merged.face_valid && merged.segmentation_valid);
    EXPECT_EQ(merged.capture_sequence, 12U);
    EXPECT_EQ(merged.model_sequence, 3U);
    EXPECT_EQ(state.completed_count, 3U);
}

TEST(
    PostprocessState,
    EmptyDetectionClearsOnlyTheUpdatedModel
)
{
    PostprocessState state;
    inference::BoxSet result{};
    result.person_valid = true;
    result.person.count = 2U;
    result.face_valid = true;
    result.face.count = 1U;
    state.Merge(result);
    result = {};
    result.face_valid = true;
    const auto &merged = state.Merge(result);
    EXPECT_EQ(merged.person.count, 2U);
    EXPECT_TRUE(merged.face_valid);
    EXPECT_EQ(merged.face.count, 0U);
    EXPECT_EQ(merged.model_sequence, 2U);
}

TEST(
    PostprocessState,
    PublicationSequencePreservesUnsignedWrap
)
{
    PostprocessState state;
    state.latest_boxes.model_sequence = UINT32_MAX;
    state.completed_count = UINT32_MAX;
    inference::BoxSet result{};
    result.capture_sequence = 7U;
    EXPECT_EQ(state.Merge(result).model_sequence, 0U);
    EXPECT_EQ(state.latest_boxes.capture_sequence, 7U);
    EXPECT_EQ(state.completed_count, 0U);
}

TEST(
    DisplayResultState,
    ModelsExpireIndependentlyAndDuplicatesCannotReviveThem
)
{
    PostprocessState producer;
    DisplayResultState display;
    inference::BoxSet result{};
    result.person_valid = true;
    result.person.count = 1U;
    producer.Merge(result, 0U);
    ASSERT_TRUE(display.Accept(producer.Snapshot()));
    result = {};
    result.face_valid = true;
    result.face.count = 1U;
    producer.Merge(result, 2000U);
    ASSERT_TRUE(display.Accept(producer.Snapshot()));
    EXPECT_EQ(display.RemainingWait(2999U), 1U);
    EXPECT_FALSE(display.Expire(2999U));
    EXPECT_TRUE(display.Expire(3000U));
    EXPECT_FALSE(display.boxes.person_valid);
    EXPECT_TRUE(display.boxes.face_valid);
    EXPECT_FALSE(display.Accept(producer.Snapshot()));
    EXPECT_FALSE(display.boxes.person_valid);
    EXPECT_TRUE(display.Expire(5000U));
    EXPECT_EQ(display.RemainingWait(5000U), UINT32_MAX);
}

TEST(
    DisplayResultState,
    ExpiryAndGenerationRemainCorrectAcrossWrap
)
{
    PostprocessState producer;
    DisplayResultState display;
    inference::BoxSet result{};
    result.segmentation_valid = true;
    producer.Merge(result, UINT32_MAX - 500U);
    const auto old = producer.Snapshot();
    ASSERT_TRUE(display.Accept(old));
    EXPECT_FALSE(display.Expire(2498U));
    EXPECT_TRUE(display.Expire(2499U));
    display.Reset(1U);
    EXPECT_FALSE(display.Accept(old));
    producer.Reset(1U);
    producer.Merge(result, 2500U);
    EXPECT_TRUE(display.Accept(producer.Snapshot()));
    EXPECT_TRUE(display.boxes.segmentation_valid);
}

class FrameChannelsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        buffers.clear();
        capacity_override = 0;
        send_failures.clear();
        receive_results.clear();
        send_calls = 0U;
        ASSERT_TRUE(memory.Initialize().Ok());
    }

    memory_manager::MemoryManager memory{};
};

TEST_F(
    FrameChannelsTest,
    FullFrameQueueReleasesOldestLease
)
{
    capacity_override = 1;
    InferenceFrameChannel channel(memory);
    ASSERT_TRUE(channel.Create().Ok());

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
    std::uint32_t generation = UINT32_MAX;
    ASSERT_TRUE(channel.Receive(&received, &generation).Ok());
    EXPECT_EQ(generation, 0U);
    EXPECT_EQ(received.lease_token, second.lease_token);
    EXPECT_EQ(channel.Receive(&received).Code(), common::ErrorCode::kNoFrame);
    EXPECT_TRUE(memory.ReleaseInferenceBuffer(received).Ok());
}

TEST_F(
    FrameChannelsTest,
    ResultQueueKeepsNewestAndDrainIgnoresInvalidResult
)
{
    InferenceResultChannel channel;
    ASSERT_TRUE(channel.Create().Ok());
    EXPECT_EQ(buffers.front().capacity, kResultQueueDepth);
    inference::BoxSet active{};
    active.model_sequence = 99U;
    EXPECT_EQ(channel.DrainLatest(&active).error.Code(), common::ErrorCode::kNoFrame);

    for (std::uint32_t sequence = 1U; sequence <= 5U; ++sequence) {
        inference::BoxSet boxes{};
        boxes.model_sequence = sequence;
        boxes.person_valid = sequence != 5U;
        ASSERT_TRUE(channel.PublishLatest(boxes).Ok());
    }
    ASSERT_TRUE(channel.DrainLatest(&active).error.Ok());
    EXPECT_EQ(active.model_sequence, 4U);
    EXPECT_EQ(channel.DrainLatest(&active).error.Code(), common::ErrorCode::kNoFrame);
    EXPECT_EQ(active.model_sequence, 4U);
}

TEST_F(
    FrameChannelsTest,
    ExposureConsumerReceivesEachFreshModelAndEmptyResult
)
{
    InferenceResultChannel channel;
    ASSERT_TRUE(channel.Create().Ok());
    inference::BoxSet face{};
    face.capture_sequence = 1;
    face.face_valid = true;
    face.face.count = 1;
    inference::BoxSet person{};
    person.capture_sequence = 2;
    person.person_valid = true;
    inference::BoxSet empty{};
    empty.capture_sequence = 3;
    empty.face_valid = true;
    ASSERT_TRUE(channel.PublishLatest(face).Ok());
    ASSERT_TRUE(channel.PublishLatest(person).Ok());
    ASSERT_TRUE(channel.PublishLatest(empty).Ok());
    std::vector<std::uint32_t> sequences;
    const auto drained = channel.Consume([&](const inference::BoxSet &result) {
        sequences.push_back(result.capture_sequence);
        if (result.capture_sequence == 2) {
            EXPECT_FALSE(result.face_valid);
        }
        if (result.capture_sequence == 3) {
            EXPECT_EQ(result.face.count, 0U);
        }
    });
    EXPECT_TRUE(drained.error.Ok());
    EXPECT_EQ(sequences, (std::vector<std::uint32_t>{1, 2, 3}));
}

TEST_F(
    FrameChannelsTest,
    ExposureIgnoresQueuedResultsFromPreviousGeneration
)
{
    ExposureResultChannel channel;
    ASSERT_TRUE(channel.Create().Ok());
    inference::BoxSet result{};
    result.capture_sequence = 10U;
    ASSERT_TRUE(channel.PublishLatest(result, 1U).Ok());
    result.capture_sequence = 20U;
    ASSERT_TRUE(channel.PublishLatest(result, 2U).Ok());
    std::vector<std::uint32_t> received;
    const auto drained = channel.Consume(
        [&](const inference::BoxSet &boxes) {
            received.push_back(boxes.capture_sequence);
        },
        2U
    );
    EXPECT_TRUE(drained.error.Ok());
    EXPECT_EQ(received, (std::vector<std::uint32_t>{20U}));
}

TEST_F(
    FrameChannelsTest,
    SnapshotReplacementRetainsEachModelStampAndNotifiesAfterPublish
)
{
    capacity_override = 1;
    ModelResultChannel channel;
    PostprocessState producer;
    ASSERT_TRUE(channel.Create().Ok());
    std::uint32_t notifications = 0U;
    channel.SetNotification(
        {&notifications,
         [](void *context, std::uint32_t) {
             ++*static_cast<std::uint32_t *>(context);
             EXPECT_FALSE(buffers.front().messages.empty());
         },
         0U}
    );
    inference::BoxSet result{};
    result.person_valid = true;
    producer.Merge(result, 0U);
    ASSERT_TRUE(channel.PublishLatest(producer.Snapshot()).Ok());
    result = {};
    result.face_valid = true;
    producer.Merge(result, 100U);
    ASSERT_TRUE(channel.PublishLatest(producer.Snapshot()).Ok());
    ModelResultSnapshot snapshot;
    ASSERT_TRUE(channel.DrainLatest(&snapshot).error.Ok());
    EXPECT_EQ(notifications, 2U);
    EXPECT_TRUE(snapshot.stamps[0U].valid);
    EXPECT_EQ(snapshot.stamps[0U].completed_ms, 0U);
    EXPECT_EQ(snapshot.stamps[1U].completed_ms, 100U);
}

TEST_F(
    FrameChannelsTest,
    ResultRetriesOnlyOnceOnOverflow
)
{
    capacity_override = 1;
    InferenceResultChannel channel;
    inference::BoxSet active{};
    const message_channel::DrainResult uninitialized = channel.DrainLatest(&active);
    EXPECT_EQ(uninitialized.error.Code(), common::ErrorCode::kNotInitialized);
    EXPECT_FALSE(uninitialized.updated);
    EXPECT_EQ(channel.PublishLatest(active).Code(), common::ErrorCode::kNotInitialized);
    EXPECT_EQ(send_calls, 0U);
    ASSERT_TRUE(channel.Create().Ok());
    active.person_valid = true;
    active.model_sequence = 1U;
    ASSERT_TRUE(channel.PublishLatest(active).Ok());

    active.model_sequence = 2U;
    send_failures.push_back(E_TMOUT);
    send_failures.push_back(E_TMOUT);
    const auto calls_before = send_calls;
    EXPECT_EQ(channel.PublishLatest(active).Code(), common::ErrorCode::kBufferOverflow);
    EXPECT_EQ(send_calls - calls_before, 2U);
    const message_channel::DrainResult invalid = channel.DrainLatest(nullptr);
    EXPECT_EQ(invalid.error.Code(), common::ErrorCode::kInvalidArgument);
    EXPECT_FALSE(invalid.updated);
    inference::BoxSet received{};
    EXPECT_EQ(channel.DrainLatest(&received).error.Code(), common::ErrorCode::kNoFrame);

    active.model_sequence = 3U;
    ASSERT_TRUE(channel.PublishLatest(active).Ok());
    send_failures.push_back(static_cast<ER>(-42));
    EXPECT_EQ(channel.PublishLatest(active).Code(), common::ErrorCode::kHardware);
    EXPECT_EQ(buffers.front().messages.size(), 1U);
    ASSERT_TRUE(channel.DrainLatest(&received).error.Ok());
    EXPECT_EQ(received.model_sequence, 3U);
}

TEST_F(
    FrameChannelsTest,
    ConsecutiveFullResultsKeepNewest
)
{
    capacity_override = 1;
    InferenceResultChannel channel;
    ASSERT_TRUE(channel.Create().Ok());
    inference::BoxSet result{};
    result.person_valid = true;
    for (std::uint32_t sequence = 1U; sequence <= 3U; ++sequence) {
        result.model_sequence = sequence;
        ASSERT_TRUE(channel.PublishLatest(result).Ok());
    }
    EXPECT_EQ(send_calls, 5U);
    inference::BoxSet active{};
    ASSERT_TRUE(channel.DrainLatest(&active).error.Ok());
    EXPECT_EQ(active.model_sequence, 3U);
}

TEST_F(
    FrameChannelsTest,
    DrainLatestKeepsLastValidResult
)
{
    InferenceResultChannel channel;
    ASSERT_TRUE(channel.Create().Ok());
    inference::BoxSet active{};
    active.model_sequence = 99U;
    inference::BoxSet result{};
    result.model_sequence = 1U;
    ASSERT_TRUE(channel.PublishLatest(result).Ok());
    const message_channel::DrainResult rejected = channel.DrainLatest(&active);
    EXPECT_EQ(rejected.error.Code(), common::ErrorCode::kNoFrame);
    EXPECT_FALSE(rejected.updated);
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
    const message_channel::DrainResult drained = channel.DrainLatest(&active);
    ASSERT_TRUE(drained.error.Ok());
    EXPECT_TRUE(drained.updated);
    EXPECT_EQ(active.model_sequence, 4U);
    const message_channel::DrainResult empty = channel.DrainLatest(&active);
    EXPECT_EQ(empty.error.Code(), common::ErrorCode::kNoFrame);
    EXPECT_FALSE(empty.updated);
    EXPECT_EQ(active.model_sequence, 4U);
}

TEST_F(
    FrameChannelsTest,
    DrainLatestReportsErrorsWithoutLosingUpdateState
)
{
    InferenceResultChannel channel;
    ASSERT_TRUE(channel.Create().Ok());
    inference::BoxSet active{};
    active.model_sequence = 99U;
    for (std::uint32_t sequence = 1U; sequence <= 3U; ++sequence) {
        inference::BoxSet boxes{};
        boxes.model_sequence = sequence;
        boxes.person_valid = sequence != 2U;
        ASSERT_TRUE(channel.PublishLatest(boxes).Ok());
    }

    receive_results = {static_cast<INT>(-42)};
    const message_channel::DrainResult failed = channel.DrainLatest(&active);
    EXPECT_EQ(failed.error.Code(), common::ErrorCode::kHardware);
    EXPECT_FALSE(failed.updated);
    EXPECT_EQ(active.model_sequence, 99U);
    EXPECT_EQ(buffers.front().messages.size(), 3U);

    receive_results = {E_OK, E_OK, static_cast<INT>(-42)};
    const message_channel::DrainResult partial = channel.DrainLatest(&active);
    EXPECT_EQ(partial.error.Code(), common::ErrorCode::kHardware);
    EXPECT_TRUE(partial.updated);
    EXPECT_EQ(active.model_sequence, 1U);
    EXPECT_EQ(buffers.front().messages.size(), 1U);

    const message_channel::DrainResult recovered = channel.DrainLatest(&active);
    EXPECT_TRUE(recovered.error.Ok());
    EXPECT_TRUE(recovered.updated);
    EXPECT_EQ(active.model_sequence, 3U);
}

TEST_F(
    FrameChannelsTest,
    FrameSendFailureReturnsTheInputLease
)
{
    InferenceFrameChannel channel(memory);
    ASSERT_TRUE(channel.Create().Ok());
    buffer::Buffer buffer{};
    ASSERT_TRUE(memory.InferenceBuffer(0U, &buffer).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(buffer.address, 1U).Ok());
    pipeline::InferenceFrame frame{};
    ASSERT_TRUE(memory.ImportCompletedInference(buffer.address, 1U, &frame).Ok());
    send_failures.push_back(static_cast<ER>(-42));
    receive_results.push_back(static_cast<INT>(-42));

    channel.Send(frame);
    EXPECT_EQ(send_calls, 1U);
    EXPECT_TRUE(memory.IsInferenceBufferFree(buffer.address));
    EXPECT_TRUE(buffers.front().messages.empty());
}

TEST_F(
    FrameChannelsTest,
    FrameNonOverflowFailureReleasesOldestThenRetries
)
{
    capacity_override = 1;
    InferenceFrameChannel channel(memory);
    ASSERT_TRUE(channel.Create().Ok());
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
    ASSERT_TRUE(channel.Receive(&received).Ok());
    EXPECT_EQ(received.lease_token, second.lease_token);
    EXPECT_TRUE(memory.ReleaseInferenceBuffer(received).Ok());
}

TEST_F(
    FrameChannelsTest,
    FrameReceiveFailureRetainsQueuedLease
)
{
    capacity_override = 1;
    InferenceFrameChannel channel(memory);
    ASSERT_TRUE(channel.Create().Ok());
    buffer::Buffer first_buffer{}, second_buffer{};
    ASSERT_TRUE(memory.InferenceBuffer(0U, &first_buffer).Ok());
    ASSERT_TRUE(memory.InferenceBuffer(1U, &second_buffer).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(first_buffer.address, 1U).Ok());
    ASSERT_TRUE(memory.ReserveCompletedInference(second_buffer.address, 2U).Ok());
    pipeline::InferenceFrame first{}, second{}, received{};
    ASSERT_TRUE(memory.ImportCompletedInference(first_buffer.address, 1U, &first).Ok());
    ASSERT_TRUE(memory.ImportCompletedInference(second_buffer.address, 2U, &second).Ok());
    channel.Send(first);
    receive_results.push_back(static_cast<INT>(-42));
    channel.Send(second);

    EXPECT_EQ(send_calls, 2U);
    EXPECT_FALSE(memory.IsInferenceBufferFree(first_buffer.address));
    EXPECT_TRUE(memory.IsInferenceBufferFree(second_buffer.address));
    ASSERT_TRUE(channel.Receive(&received).Ok());
    EXPECT_EQ(received.lease_token, first.lease_token);
    EXPECT_TRUE(memory.ReleaseInferenceBuffer(received).Ok());
}

TEST_F(
    FrameChannelsTest,
    ResultOwnsMaskAfterSourceIsReused
)
{
    InferenceResultChannel channel;
    ASSERT_TRUE(channel.Create().Ok());

    std::uint8_t source[inference::kSegmentationMaskWidth * inference::kSegmentationMaskHeight]{};
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
    ASSERT_TRUE(channel.DrainLatest(&displayed).error.Ok());
    EXPECT_EQ(displayed.segmentation.mask.data()[0], 1U);
    EXPECT_EQ(
        displayed.segmentation.mask.CopyFrom(source, sizeof(source) + 1U).Code(), common::ErrorCode::kBufferOverflow
    );
    EXPECT_EQ(displayed.segmentation.mask.data()[0], 1U);
}

} // namespace
} // namespace uai::ai::task