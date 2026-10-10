#include "tests/framework.hpp"
#include "driver/peripheral_driver/peripheral_driver.hpp"

#include <cstdio>

namespace uai::hwtest::tests::tim_driver {

Result Run(const Context &context)
{
    using uai::ai::peripheral::CounterSample;
    uai::ai::peripheral::PeripheralManagement::Accessor accessor;
    if (!uai::ai::peripheral::PeripheralManagement::Instance().Acquire(&accessor, 100).Ok()) {
        return {Outcome::kFail, "tim2-ownership"};
    }
    if (!accessor->StartCounter(1000000, accessor.Ownership()).Ok()) {
        return {Outcome::kFail, "tim2-initialization-or-start"};
    }
    Result result{Outcome::kPass, "tim2-counter-rate-and-stop"};
    for (const auto interval : {25U, 100U}) {
        CounterSample before{}, after{};
        if (!accessor->ReadCounter(&before, accessor.Ownership()).Ok()) {
            result = {Outcome::kFail, "tim2-counter-read"};
            break;
        }
        const auto begin = context.clock();
        context.wait(interval);
        const auto elapsed = context.clock() - begin;
        if (!accessor->ReadCounter(&after, accessor.Ownership()).Ok()) {
            result = {Outcome::kFail, "tim2-counter-read"};
            break;
        }
        const auto ticks = static_cast<std::uint32_t>(after.ticks - before.ticks);
        const auto cycles = static_cast<std::uint32_t>(after.cycles - before.cycles);
        const auto expected = before.cycles_available
            ? static_cast<std::uint64_t>(cycles) * before.frequency / before.cycle_frequency
            : static_cast<std::uint64_t>(elapsed) * before.frequency / 1000;
        char line[160];
        std::snprintf(
            line,
            sizeof(line),
            "TRACE tim sample request=%lu ms=%lu ticks=%lu expected=%llu cycles=%lu",
            static_cast<unsigned long>(interval),
            static_cast<unsigned long>(elapsed),
            static_cast<unsigned long>(ticks),
            static_cast<unsigned long long>(expected),
            static_cast<unsigned long>(cycles)
        );
        context.Trace(line);
        if ((before.cycles_available ? cycles == 0 : elapsed == 0)
            || static_cast<std::uint64_t>(ticks) + 2000 < expected || ticks > expected + 2000) {
            result = {Outcome::kFail, "tim2-counter-rate"};
            break;
        }
    }
    CounterSample stopped{}, after_wait{};
    if (!accessor->StopCounter(accessor.Ownership()).Ok()
        || !accessor->ReadCounter(&stopped, accessor.Ownership()).Ok()) {
        result = {Outcome::kFail, "tim2-stop"};
    } else {
        context.wait(5);
        if (!accessor->ReadCounter(&after_wait, accessor.Ownership()).Ok() || after_wait.ticks != stopped.ticks) {
            result = {Outcome::kFail, "tim2-running-after-stop"};
        }
    }
    return accessor.Close().Ok() ? result : Result{Outcome::kFail, "tim2-deinitialization"};
}

}