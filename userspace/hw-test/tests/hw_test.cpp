#include "commands.hpp"
#include "graphics/dma2d.hpp"
#include "graphics/verification.hpp"
#include "tests/suite.hpp"
#include "ui/ui_layout.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <type_traits>
#include <vector>

namespace {

namespace hwtest = experiment::hwtest;
namespace graphics = experiment::graphics;
namespace shared = uai::ai::image_processing;

static_assert(std::is_same_v<
              graphics::Request,
              shared::Request>);
static_assert(std::is_same_v<
              graphics::Verification,
              shared::Verification>);
static_assert(std::is_same_v<
              graphics::Result,
              shared::VerificationResult>);

TEST(
    HwTestRegistry,
    PreservesSeventeenCasesAndQuickStressSelections
)
{
    const char *names[] = {
        "display",
        "rng",
        "hash",
        "crc",
        "gpdma",
        "hpdma",
        "rtc",
        "tim",
        "sram",
        "psram",
        "nor-read",
        "dma2d",
        "camera-pipes",
        "camera-control",
        "dma2d-suite",
        "touch-read",
        "touch"
    };
    ASSERT_EQ(hwtest::tests::case_count, std::size(names));
    for (std::size_t index = 0; index < std::size(names); ++index) {
        EXPECT_STREQ(hwtest::tests::cases[index].name, names[index]);
        EXPECT_NE(hwtest::tests::cases[index].run, nullptr);
        EXPECT_GT(hwtest::tests::cases[index].timeout_ms, 0U);
    }
    EXPECT_EQ(hwtest::CountSelected(hwtest::tests::cases, hwtest::tests::case_count, "all"), 13U);
    EXPECT_EQ(hwtest::CountSelected(hwtest::tests::cases, hwtest::tests::case_count, "all-stress"), 16U);
    EXPECT_EQ(hwtest::CountSelected(hwtest::tests::cases, hwtest::tests::case_count, "touch"), 1U);
}

TEST(
    HwTestRegistry,
    RefusesDestructiveTestsWithoutPermission
)
{
    const hwtest::Case cases[] = {
        {"scratch",
         [](const hwtest::Context &) {
             ADD_FAILURE() << "destructive test executed without permission";
             return hwtest::Result{hwtest::Outcome::kPass, "unexpected"};
         },
         true,
         100,
         "scratch write"}
    };
    std::string log;
    const auto summary = hwtest::Run(
        cases,
        1,
        "all",
        false,
        {&log,
         [](void *context, const char *line) {
             static_cast<std::string *>(context)->append(line);
         }},
        {[] {
             return 0U;
         },
         [](std::uint32_t) {},
         nullptr}
    );
    EXPECT_EQ(summary.failed, 1U);
    EXPECT_EQ(summary.total, 1U);
    EXPECT_NE(log.find("explicit-permission-required"), std::string::npos);
    EXPECT_NE(log.find("HWTEST SUMMARY pass=0 fail=1 total=1 skip=0"), std::string::npos);
}

TEST(
    HwTestUi,
    SharedScreenPaintPreservesFramebufferGuards
)
{
    constexpr std::uint16_t guard = 0xa5a5;
    std::vector<std::uint16_t> pixels(800 * 480 + 64, guard);
    std::fill(pixels.begin() + 32, pixels.end() - 32, 0);
    uai::ai::ui::Canvas canvas(pixels.data() + 32, 800, 480);
    uai::ai::ui::Screen screen(uai::ai::hwtest_layout::kScreens[0]);
    screen.Paint(canvas);
    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.begin() + 32, [](std::uint16_t pixel) {
        return pixel == guard;
    }));
    EXPECT_TRUE(std::all_of(pixels.end() - 32, pixels.end(), [](std::uint16_t pixel) {
        return pixel == guard;
    }));
    EXPECT_TRUE(std::any_of(pixels.begin() + 32, pixels.end() - 32, [](std::uint16_t pixel) {
        return pixel != 0;
    }));
}

struct Device {
    bool
    Run(const graphics::Request &request,
        std::uint32_t)
    {
        return request.operation != graphics::Operation::kResize
            && reinterpret_cast<std::uintptr_t>(request.destination.data) % 32 == 0 && graphics::Reference(request);
    }
};

TEST(
    HwTestGraphics,
    AllCasesUseSharedVerificationAndKeepExperimentContract
)
{
    shared::Verification verification;
    Device device;
    ASSERT_EQ(graphics::kCaseCount, std::size(shared::kVerificationCases));
    for (std::size_t index = 0; index < graphics::kCaseCount; ++index) {
        const auto &test = graphics::kCases[index];
        const auto &reference = shared::kVerificationCases[index];
        SCOPED_TRACE(test.name);
        EXPECT_STREQ(test.name, reference.name);
        EXPECT_EQ(test.operation, reference.operation);
        EXPECT_EQ(test.source, reference.source);
        EXPECT_EQ(test.destination, reference.destination);
        EXPECT_EQ(test.alpha, reference.alpha);
        EXPECT_EQ(test.color, reference.color);
        EXPECT_EQ(test.padded, reference.padded);
        EXPECT_EQ(test.rejection, reference.rejection);
        EXPECT_TRUE(verification
                        .Run(
                            test,
                            device,
                            [] {
                                return 0U;
                            }
                        )
                        .passed);
    }
}

class Backend final : public graphics::ScenarioBackend {
public:
    graphics::Observation Observe() override { return observation; }
    graphics::Result Run(const graphics::Case &test) override
    {
        return verification.Run(test, device, [] {
            return 0U;
        });
    }
    void Report(
        const char *name,
        const graphics::Result &result
    ) override
    {
        names.emplace_back(name);
        EXPECT_TRUE(result.passed);
    }
    void Summary(
        unsigned passed,
        unsigned failed,
        std::uint32_t transfers
    ) override
    {
        passed_count = passed;
        failed_count = failed;
        transfer_count = transfers;
    }

    graphics::Observation observation{0, 0, 0, 0, true};
    std::vector<std::string> names;
    unsigned passed_count = 0;
    unsigned failed_count = 0;
    std::uint32_t transfer_count = 0;

private:
    Device device;
    shared::Verification verification;
};

TEST(
    HwTestGraphics,
    ScenarioPreserves25CasesAndSixtySecondConcurrentStress
)
{
    Backend backend;
    graphics::Scenario scenario(backend);
    ASSERT_TRUE(scenario.Start(0));
    for (std::uint32_t now = 0; now <= 90000 && scenario.Active(); now += 1000) {
        ++backend.observation.pipe1;
        ++backend.observation.pipe2;
        scenario.Tick(now);
    }
    EXPECT_FALSE(scenario.Active());
    EXPECT_EQ(backend.passed_count, graphics::kCaseCount + 1);
    EXPECT_EQ(backend.failed_count, 0U);
    EXPECT_GT(backend.transfer_count, 0U);
    ASSERT_EQ(backend.names.size(), graphics::kCaseCount + 1);
    EXPECT_EQ(backend.names.back(), "camera-display-stress-60s");
}

}

#define HWTEST_HOST_CASE(name)                                                                                         \
    namespace experiment::hwtest::tests::name {                                                                        \
    Result Run(const Context &)                                                                                        \
    {                                                                                                                  \
        return {Outcome::kFail, "hardware-not-executed-on-host"};                                                      \
    }                                                                                                                  \
    }

HWTEST_HOST_CASE(display_driver)
HWTEST_HOST_CASE(rng_driver)
HWTEST_HOST_CASE(hash_driver)
HWTEST_HOST_CASE(crc_driver)
HWTEST_HOST_CASE(gpdma_driver)
HWTEST_HOST_CASE(hpdma_driver)
HWTEST_HOST_CASE(rtc_driver)
HWTEST_HOST_CASE(tim_driver)
HWTEST_HOST_CASE(sram_driver)
HWTEST_HOST_CASE(psram_driver)
HWTEST_HOST_CASE(nor_driver)
HWTEST_HOST_CASE(dma2d_driver)
#undef HWTEST_HOST_CASE

namespace experiment::hwtest::integrated {
Result CameraPipes(const Context &)
{
    return {Outcome::kFail, "hardware-not-executed-on-host"};
}
Result CameraControl(const Context &)
{
    return {Outcome::kFail, "hardware-not-executed-on-host"};
}
Result Dma2dSuite(const Context &)
{
    return {Outcome::kFail, "hardware-not-executed-on-host"};
}
Result TouchRead(const Context &)
{
    return {Outcome::kFail, "hardware-not-executed-on-host"};
}
Result TouchInteractive(const Context &)
{
    return {Outcome::kFail, "hardware-not-executed-on-host"};
}
}