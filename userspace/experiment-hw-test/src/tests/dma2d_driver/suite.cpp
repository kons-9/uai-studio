#include "integration.hpp"
#include "graphics/dma2d.hpp"
#include "graphics/scenario.hpp"
#include "graphics/verification.hpp"

namespace experiment::hwtest::integrated {
namespace {
class Backend final : public graphics::ScenarioBackend {
public:
    explicit Backend(const Context &context) : context_(context) {}
    graphics::Observation Observe() override
    {
        const auto observation = integrated::Observe();
        return {observation.pipe1, observation.pipe2, observation.errors, observation.recoveries, observation.running};
    }
    graphics::Result Run(const graphics::Case &test) override
    {
        const graphics::VerificationCache cache{
            [](void *address, std::int32_t bytes) { SCB_CleanInvalidateDCache_by_Addr(address, bytes); __DSB(); },
            [](void *address, std::int32_t bytes) { __DSB(); SCB_InvalidateDCache_by_Addr(address, bytes); __DSB(); }
        };
        return verification_.Run(test, dma_, [] { return DWT->CYCCNT; }, cache);
    }
    void Report(const char *name, const graphics::Result &result) override
    {
        char line[192];
        std::snprintf(line, sizeof(line), "TRACE dma2d case=%s %s cycles=%lu max_channel_error=%u corrupted_bytes=%u",
            name, result.passed ? "PASS" : "FAIL", static_cast<unsigned long>(result.cycles), result.maximum_error, result.corrupted_bytes);
        context_.Trace(line);
    }
    void Summary(unsigned passed, unsigned failed, std::uint32_t transfers) override
    {
        passed_ = passed;
        failed_ = failed;
        transfers_ = transfers;
    }
    unsigned passed_ = 0, failed_ = 0;
    std::uint32_t transfers_ = 0;

private:
    const Context &context_;
    graphics::Dma2d dma_;
    graphics::Verification verification_;
};
}

Result Dma2dSuite(const Context &context)
{
    if (!Observe().running) {
        return {Outcome::kFail, "camera-not-running"};
    }
    static char detail[128];
    static Backend backend(context);
    graphics::Scenario scenario(backend);
    const auto begin = context.clock();
    scenario.Start(begin);
    while (scenario.Active()) {
        if (context.Cancelled() || context.Expired(begin, 85000)) {
            return {Outcome::kFail, "cancelled-or-deadline"};
        }
        scenario.Tick(context.clock());
        context.wait(10);
    }
    std::snprintf(detail, sizeof(detail), "cases_pass=%u cases_fail=%u transfers=%lu gpu2d=not-tested visual=required",
        backend.passed_, backend.failed_, static_cast<unsigned long>(backend.transfers_));
    return {backend.passed_ == graphics::kCaseCount + 1 && backend.failed_ == 0 ? Outcome::kPass : Outcome::kFail, detail};
}

}