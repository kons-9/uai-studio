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
};
struct Summary {
    unsigned passed = 0, failed = 0;
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

inline Summary
Run(const Case *cases,
    std::size_t count,
    const char *selection,
    bool allow_destructive,
    Output output,
    const Context &context)
{
    Summary summary;
    bool matched = false;
    if (!context.clock || !context.wait) {
        output.write(output.context, "HWTEST registry FAIL invalid-context\nHWTEST SUMMARY pass=0 fail=1 skip=0\n");
        return {0, 1};
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!ValidName(cases[index].name) || !cases[index].run || cases[index].timeout_ms == 0 || !cases[index].purpose
            || !*cases[index].purpose) {
            output.write(output.context, "HWTEST registry FAIL invalid-case\nHWTEST SUMMARY pass=0 fail=1 skip=0\n");
            return {0, 1};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::strcmp(cases[index].name, cases[previous].name) == 0) {
                output.write(
                    output.context, "HWTEST registry FAIL duplicate-case\nHWTEST SUMMARY pass=0 fail=1 skip=0\n"
                );
                return {0, 1};
            }
        }
        if ((!selection || std::strcmp(selection, cases[index].name) == 0) && cases[index].destructive
            && !allow_destructive) {
            output.write(
                output.context,
                "HWTEST selection FAIL explicit-permission-required\nHWTEST SUMMARY pass=0 fail=1 skip=0\n"
            );
            return {0, 1};
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto &test = cases[index];
        if (selection && std::strcmp(selection, test.name) != 0) {
            continue;
        }
        matched = true;
        const auto begin = context.clock();
        Result result = test.run(context);
        if (context.clock() - begin > test.timeout_ms) {
            result = {Outcome::kFail, "deadline-exceeded"};
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
        char detail[96]{};
        if (result.detail) {
            for (std::size_t offset = 0; offset + 1 < sizeof(detail) && result.detail[offset]; ++offset) {
                const auto character = static_cast<unsigned char>(result.detail[offset]);
                detail[offset] = character >= 32 && character < 127 ? static_cast<char>(character) : ' ';
            }
        }
        char line[192];
        std::snprintf(line, sizeof(line), "HWTEST %s %s %s\n", test.name, status, detail);
        output.write(output.context, line);
    }
    if (!matched) {
        output.write(output.context, "HWTEST selection FAIL no-matching-test\n");
        ++summary.failed;
    }
    char line[96];
    std::snprintf(line, sizeof(line), "HWTEST SUMMARY pass=%u fail=%u skip=0\n", summary.passed, summary.failed);
    output.write(output.context, line);
    return summary;
}

}