#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace experiment::hwtest {

enum class Outcome {
    kPass,
    kFail
};
struct Result {
    Outcome outcome;
    const char *detail;
};
struct Context {
    std::uint32_t (*clock)();
    void (*wait)(std::uint32_t);
    void (*trace)(const char *);
    bool (*cancelled)() = nullptr;
    void (*progress)(
        const char *,
        unsigned,
        unsigned
    ) = nullptr;
    const char *test_name = nullptr;

    bool Cancelled() const { return cancelled && cancelled(); }

    void Trace(const char *line) const
    {
        if (trace) {
            trace(line);
        }
    }

    void Progress(
        unsigned current,
        unsigned total
    ) const
    {
        if (progress && test_name) {
            progress(test_name, current, total);
        }
    }

    bool Expired(
        std::uint32_t begin,
        std::uint32_t timeout_ms
    ) const
    {
        return clock() - begin >= timeout_ms;
    }
};
struct Case {
    const char *name;
    Result (*run)(const Context &);
    bool destructive;
    std::uint32_t timeout_ms;
    const char *purpose;
    bool interactive = false;
    bool stress = false;
};
struct Summary {
    unsigned passed = 0, failed = 0, total = 0;
};
struct Output {
    void *context;
    void (*write)(
        void *,
        const char *
    );
};

inline bool ValidName(const char *name)
{
    if (!name || *name == '\0') {
        return false;
    }
    std::size_t length = 0;
    for (; *name; ++name, ++length) {
        if (length >= 48
            || !(
                (*name >= 'a' && *name <= 'z') || (*name >= 'A' && *name <= 'Z') || (*name >= '0' && *name <= '9')
                || *name == '-' || *name == '_'
            )) {
            return false;
        }
    }
    return true;
}

inline void WriteSummary(
    Output output,
    const Summary &summary
)
{
    char line[112];
    std::snprintf(
        line,
        sizeof(line),
        "HWTEST SUMMARY pass=%u fail=%u total=%u skip=0\n",
        summary.passed,
        summary.failed,
        summary.total
    );
    output.write(output.context, line);
}

inline Summary FailSelection(
    Output output,
    const char *reason
)
{
    char line[128];
    std::snprintf(line, sizeof(line), "HWTEST selection FAIL %s\n", reason);
    output.write(output.context, line);
    Summary summary{0, 1, 1};
    WriteSummary(output, summary);
    return summary;
}

inline bool Selected(
    const Case &test,
    const char *selection
)
{
    if (!selection || std::strcmp(selection, "all") == 0) {
        return !test.interactive && !test.stress;
    }
    if (std::strcmp(selection, "all-stress") == 0) {
        return !test.interactive;
    }
    return std::strcmp(selection, test.name) == 0;
}

inline unsigned CountSelected(
    const Case *cases,
    std::size_t count,
    const char *selection
)
{
    unsigned selected = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (Selected(cases[index], selection)) {
            ++selected;
        }
    }
    // Invalid or empty selections produce one failure result in Run().
    return selected == 0 ? 1 : selected;
}

inline Summary
Run(const Case *cases,
    std::size_t count,
    const char *selection,
    bool allow_destructive,
    Output output,
    const Context &context,
    Output progress = {
        nullptr,
        nullptr
    })
{
    Summary summary;
    bool matched = false;
    if (!context.clock || !context.wait) {
        return FailSelection(output, "invalid-context");
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!ValidName(cases[index].name) || !cases[index].run || cases[index].timeout_ms == 0 || !cases[index].purpose
            || !*cases[index].purpose) {
            return FailSelection(output, "invalid-case");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::strcmp(cases[index].name, cases[previous].name) == 0) {
                return FailSelection(output, "duplicate-case");
            }
        }
        if (Selected(cases[index], selection) && cases[index].destructive && !allow_destructive) {
            return FailSelection(output, "explicit-permission-required");
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto &test = cases[index];
        if (!Selected(test, selection)) {
            continue;
        }
        matched = true;
        if (context.Cancelled()) {
            output.write(output.context, "HWTEST selection FAIL cancelled\n");
            ++summary.failed;
            ++summary.total;
            break;
        }
        if (progress.write) {
            char line[64];
            std::snprintf(line, sizeof(line), "HWTEST %s START\n", test.name);
            progress.write(progress.context, line);
        }
        const auto begin = context.clock();
        Context test_context = context;
        test_context.test_name = test.name;
        Result result = test.run(test_context);
        ++summary.total;
        if (context.Cancelled()) {
            result = {Outcome::kFail, "cancelled"};
        }
        const auto elapsed = context.clock() - begin;
        char timeout_detail[128]{};
        if (elapsed > test.timeout_ms) {
            std::snprintf(
                timeout_detail,
                sizeof(timeout_detail),
                "deadline-exceeded elapsed_ms=%lu cause=%.80s",
                static_cast<unsigned long>(elapsed),
                result.detail ? result.detail : "unknown"
            );
            result = {Outcome::kFail, timeout_detail};
        }
        const char *status = "FAIL";
        switch (result.outcome) {
        case Outcome::kPass:
            ++summary.passed;
            status = "PASS";
            break;
        default:
            ++summary.failed;
            break;
        }
        char detail[128]{};
        if (result.detail) {
            for (std::size_t offset = 0; offset + 1 < sizeof(detail) && result.detail[offset]; ++offset) {
                const auto character = static_cast<unsigned char>(result.detail[offset]);
                detail[offset] = character >= 32 && character < 127 ? static_cast<char>(character) : ' ';
            }
        }
        char line[192];
        std::snprintf(line, sizeof(line), "HWTEST %s %s %s\n", test.name, status, detail);
        output.write(output.context, line);
        if (context.Cancelled()) {
            break;
        }
    }
    if (!matched) {
        output.write(output.context, "HWTEST selection FAIL no-matching-test\n");
        ++summary.failed;
        ++summary.total;
    }
    WriteSummary(output, summary);
    return summary;
}

}
