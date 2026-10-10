#include "tests/framework.hpp"
#include "driver/nor_driver/nor_driver.hpp"

#include <cstdio>

namespace uai::hwtest::tests::nor_driver {
namespace {

Result Failure(
    const uai::ai::nor::NorManagement::Accessor &accessor,
    const char *stage
)
{
    const auto diagnostic = accessor.Diagnostics();
    static char detail[160];
    std::snprintf(
        detail,
        sizeof(detail),
        "%s diag=%lu/%lu result=%ld err=%08lx state=%lu status=%08lx command=%08lx",
        stage,
        static_cast<unsigned long>(diagnostic.code),
        static_cast<unsigned long>(diagnostic.stage),
        static_cast<long>(diagnostic.result),
        static_cast<unsigned long>(diagnostic.error),
        static_cast<unsigned long>(diagnostic.state),
        static_cast<unsigned long>(diagnostic.status),
        static_cast<unsigned long>(diagnostic.command)
    );
    return {Outcome::kFail, detail};
}

}

Result Run(const Context &context)
{
    uai::ai::nor::NorManagement::Accessor accessor;
    const auto init_begin = context.clock();
    if (!uai::ai::nor::NorManagement::Instance().OpenReadOnly(&accessor, 100).Ok()) {
        return Failure(accessor, "nor-initialization");
    }
    const auto init_ms = context.clock() - init_begin;
    std::uint8_t first[256]{}, second[256]{};
    const auto first_begin = context.clock();
    if (!accessor.Read(0, first, sizeof(first)).Ok())
        return Failure(accessor, "nor-read1");
    const auto read1_ms = context.clock() - first_begin;
    const auto second_begin = context.clock();
    if (!accessor.Read(0, second, sizeof(second)).Ok())
        return Failure(accessor, "nor-read2");
    const auto read2_ms = context.clock() - second_begin;
    for (std::size_t offset = 0; offset < sizeof(first); ++offset) {
        if (first[offset] != second[offset])
            return {Outcome::kFail, "nor-repeat-read-unstable"};
    }
    static char detail[96];
    std::snprintf(
        detail,
        sizeof(detail),
        "init_ms=%lu read1_ms=%lu read2_ms=%lu compare=stable",
        static_cast<unsigned long>(init_ms),
        static_cast<unsigned long>(read1_ms),
        static_cast<unsigned long>(read2_ms)
    );
    context.Trace(detail);
    return {Outcome::kPass, detail};
}

}