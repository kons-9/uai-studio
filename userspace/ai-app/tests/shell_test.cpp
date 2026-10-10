#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "shell/engine.hpp"
#include "shell/mailbox.hpp"
#include "shell/commands.hpp"
#include "shell/owner.hpp"
#include "shell/trace_stream.hpp"
#include "middleware/foundation/log.hpp"

namespace {

using uai::ai::shell::Engine;
using uai::ai::shell::Output;

struct Fixture {
    std::string output;
    std::vector<std::string> arguments;
    Engine engine{{this, [](void *context, const char *data, std::size_t size) {
                       static_cast<Fixture *>(context)->output.append(data, size);
                   }}};

    Fixture()
    {
        EXPECT_TRUE(engine.Register({"echo", "echo <text>", [](int count, const char *const *args, const Output &out, void *context) {
                                          auto &fixture = *static_cast<Fixture *>(context);
                                          for (int index = 0; index < count; ++index)
                                              fixture.arguments.emplace_back(args[index]);
                                          out.Write("ok\r\n");
                                      }, this}));
    }

    void Send(const std::string &input)
    {
        for (char value : input)
            engine.Feed(value);
    }
};

TEST(ShellEngine, DispatchesLineEditedArgumentsAndCrLfOnce)
{
    Fixture fixture;
    fixture.Send("echo worlx\b\bld\r\n");
    ASSERT_EQ(fixture.arguments.size(), 2U);
    EXPECT_EQ(fixture.arguments[0], "echo");
    EXPECT_EQ(fixture.arguments[1], "world");
    EXPECT_EQ(fixture.output, "ok\r\n");
    fixture.Send("unknown\n");
    EXPECT_EQ(fixture.output, "ok\r\nerror: unknown command\r\n");
    EXPECT_FALSE(fixture.engine.Register({"echo", "again", [](int, const char *const *, const Output &, void *) {}, nullptr}));
}

TEST(ShellEngine, DiscardsOverlongAndCorruptedLines)
{
    Fixture fixture;
    fixture.Send(std::string(Engine::kLineCapacity, 'x') + "echo bad\n");
    EXPECT_TRUE(fixture.arguments.empty());
    EXPECT_EQ(fixture.output, "error: line too long\r\n");
    fixture.Send("echo bad");
    fixture.engine.Feed(0, true);
    fixture.Send("echo stale\n");
    EXPECT_TRUE(fixture.arguments.empty());
    fixture.Send("echo fresh\n");
    ASSERT_EQ(fixture.arguments.size(), 2U);
    EXPECT_EQ(fixture.arguments[1], "fresh");
}

TEST(ShellEngine, RejectsArgumentOverflow)
{
    Fixture fixture;
    fixture.Send("echo 1 2 3 4 5 6 7 8\n");
    EXPECT_TRUE(fixture.arguments.empty());
    EXPECT_EQ(fixture.output, "error: too many arguments\r\n");
}

TEST(ShellMailbox, OwnerAcknowledgesRequestBeforeNextCommand)
{
    uai::ai::shell::Mailbox mailbox;
    const uai::ai::shell::Request request{uai::ai::shell::Action::kCameraAe, {1, 0, 0, 0}};
    EXPECT_TRUE(mailbox.Post(request));
    EXPECT_FALSE(mailbox.Post(request));
    uai::ai::shell::Request taken{};
    ASSERT_TRUE(mailbox.Take(&taken));
    EXPECT_EQ(taken.action, uai::ai::shell::Action::kCameraAe);
    EXPECT_EQ(taken.values[0], 1);
    EXPECT_FALSE(mailbox.Take(&taken));
    EXPECT_FALSE(mailbox.Post(request));
    mailbox.Complete({0, "applied"});
    uai::ai::shell::Reply reply{};
    EXPECT_TRUE(mailbox.Receive(&reply));
    EXPECT_STREQ(reply.text, "applied");
    EXPECT_TRUE(mailbox.Post(request));
}

struct FakeCamera {
    uai::ai::camera::State state{};
    uai::ai::camera::Geometry geometry{};
    bool fail_auto = false;
    bool fail_read = false;
    struct Counters {
        unsigned frame_event_count = 4;
        unsigned pipe2_frame_event_count = 3;
        unsigned dcmipp_error_count = 0;
        unsigned csi_error_count = 0;
        unsigned isp_error_count = 0;
    };
    uai::ai::common::Error ReadState(uai::ai::camera::State *output)
    {
        if (fail_read)
            return {uai::ai::common::ErrorCode::kHardware};
        *output = state;
        return {};
    }
    uai::ai::common::Error GetGeometry(uai::ai::camera::Geometry *output) { *output = geometry; return {}; }
    Counters GetDiagnostics() const { return {}; }
    uai::ai::common::Error AutoExposure(bool enabled)
    {
        if (fail_auto)
            return {uai::ai::common::ErrorCode::kHardware};
        state.auto_exposure = enabled;
        return {};
    }
    uai::ai::common::Error Compensation(int value) { state.compensation = value; return {}; }
    uai::ai::common::Error Manual(std::int32_t exposure, std::int32_t gain)
    {
        state.reported_exposure_us = exposure;
        state.reported_gain_mdB = gain;
        return {};
    }
    uai::ai::common::Error Statistics(uai::ai::camera::Rect area) { state.statistics = area; return {}; }
    uai::ai::common::Error Configure(const uai::ai::camera::Geometry &next) { geometry = next; return {}; }
};

struct FakeUi {
    bool enabled = true;
    bool boxes = true;
    std::uint8_t mask = 7;
    bool AiExposureEnabled() const { return enabled; }
    void SetAiExposureEnabled(bool value) { enabled = value; }
    bool ShowBoxes() const { return boxes; }
    void SetShowBoxes(bool value) { boxes = value; }
    std::uint8_t ModelMask() const { return mask; }
    void SetModelMask(std::uint8_t value) { mask = value; }
};

TEST(ShellOwner, RejectsConflictsAndReadsBackAppliedSettings)
{
    FakeCamera camera;
    FakeUi ui;
    uai::ai::shell::ExposureMode mode{};
    using uai::ai::shell::Action;
    using uai::ai::shell::Apply;
    EXPECT_NE(Apply({Action::kCameraAe, {0}}, camera, ui, mode).code, 0);
    EXPECT_TRUE(camera.state.auto_exposure);
    EXPECT_EQ(Apply({Action::kAiExposure, {0}}, camera, ui, mode).code, 0);
    EXPECT_EQ(Apply({Action::kCameraAe, {0}}, camera, ui, mode).code, 0);
    EXPECT_TRUE(mode.manual);
    EXPECT_EQ(Apply({Action::kCameraManual, {9000, 1200}}, camera, ui, mode).code, 0);
    EXPECT_EQ(camera.state.reported_gain_mdB, 1200);
    EXPECT_EQ(Apply({Action::kCameraStatistics, {10, 20, 100, 80}}, camera, ui, mode).code, 0);
    EXPECT_TRUE(mode.custom_statistics);
    EXPECT_EQ(Apply({Action::kAiExposure, {1}}, camera, ui, mode).code, 0);
    EXPECT_TRUE(camera.state.auto_exposure);
    EXPECT_TRUE(ui.AiExposureEnabled());
    EXPECT_FALSE(mode.custom_statistics);
    EXPECT_EQ(Apply({Action::kModels, {4}}, camera, ui, mode).code, 0);
    EXPECT_EQ(ui.ModelMask(), 4);
    EXPECT_NE(std::string(Apply({Action::kCameraStatus, {}}, camera, ui, mode).text).find("frames=4/3"), std::string::npos);
    camera.fail_read = true;
    const auto model_reply = Apply({Action::kModels, {2}}, camera, ui, mode);
    EXPECT_EQ(model_reply.code, 0);
    EXPECT_NE(std::string(model_reply.text).find("models=2"), std::string::npos);
    EXPECT_EQ(Apply({Action::kBoxes, {0}}, camera, ui, mode).code, 0);
    EXPECT_FALSE(ui.ShowBoxes());
}

TEST(ShellOwner, RestoresAutomaticExposureOnUiTransitionAndRetriesFailure)
{
    FakeCamera camera;
    uai::ai::shell::ExposureMode mode{true, true};
    camera.state.auto_exposure = false;
    camera.fail_auto = true;
    EXPECT_FALSE(uai::ai::shell::FollowUiExposure(true, false, camera, mode).Ok());
    EXPECT_TRUE(mode.manual);
    EXPECT_FALSE(mode.custom_statistics);
    camera.fail_auto = false;
    EXPECT_TRUE(uai::ai::shell::FollowUiExposure(true, true, camera, mode).Ok());
    EXPECT_TRUE(camera.state.auto_exposure);
    EXPECT_FALSE(mode.manual);
}

TEST(ShellCommands, ValidatesArgumentsAndRoutesRequests)
{
    Fixture fixture;
    uai::ai::shell::Mailbox mailbox;
    uai::ai::shell::Context context{fixture.engine, mailbox};
    context.now = [] { return 1234U; };
    context.wait_ms = [](std::uint32_t) {};
    context.list_tasks = [](const Output &out) { out.Write("task id=1\r\n"); };
    context.memory_usage = [](const Output &out) { out.Write("memory ok\r\n"); };
    context.transfer_trace = [](const Output &out, bool) { out.Write("trace ok\r\n"); };
    ASSERT_TRUE(uai::ai::shell::RegisterAll(fixture.engine, context));
    fixture.Send("help camera\n");
    EXPECT_NE(fixture.output.find("camera [status"), std::string::npos);
    fixture.Send("uptime\n");
    EXPECT_NE(fixture.output.find("uptime 1234 ms"), std::string::npos);
    fixture.Send("camera fps 11\n");
    EXPECT_NE(fixture.output.find("usage: camera"), std::string::npos);
    fixture.Send("camera manual 9000 -1\n");
    EXPECT_NE(fixture.output.find("usage: camera"), std::string::npos);
    fixture.Send("log debug\n");
    EXPECT_EQ(uai::ai::common::GetLogLevel(), uai::ai::common::LogLevel::kDebug);
    uai::ai::common::SetLogLevel(uai::ai::common::kLogLevel);
    fixture.Send("models face\n");
    uai::ai::shell::Request request{};
    ASSERT_TRUE(mailbox.Take(&request));
    EXPECT_EQ(request.action, uai::ai::shell::Action::kModels);
    EXPECT_EQ(request.values[0], 2);
    mailbox.Complete({0, "ok\r\n"});
    fixture.Send("ui exposure off\n");
    ASSERT_TRUE(mailbox.Take(&request));
    EXPECT_EQ(request.action, uai::ai::shell::Action::kAiExposure);
    EXPECT_EQ(request.values[0], 0);
}

TEST(ShellTrace, SendsVersionLengthAndCrcWithOffsetLines)
{
    Fixture fixture;
    const std::uint8_t bytes[] = {0U, 1U, 0xffU};
    uai::ai::shell::StreamTrace({&fixture, [](void *context, const char *text, std::size_t size) {
        static_cast<Fixture *>(context)->output.append(text, size);
    }}, "ai", 5U, bytes, sizeof(bytes));
    EXPECT_EQ(uai::ai::shell::TraceCrc(bytes, sizeof(bytes)), 0xcb5807deU);
    EXPECT_NE(fixture.output.find("@TRACE BEGIN format=ai version=5 length=3 crc=cb5807de\r\n"), std::string::npos);
    EXPECT_NE(fixture.output.find("@TRACE 00000000 0001ff\r\n"), std::string::npos);
    EXPECT_NE(fixture.output.find("@TRACE END crc=cb5807de\r\n"), std::string::npos);
}

} // namespace