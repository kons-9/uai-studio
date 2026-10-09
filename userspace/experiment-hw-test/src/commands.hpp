#pragma once

#include "hwtest.hpp"
#include "shell.hpp"
#include "display_log.hpp"

namespace experiment::hwtest {

struct Registry {
    const Case *cases;
    std::size_t count;
    Context context;
};

inline console::Status Execute(
    void *context,
    int count,
    const char *const *arguments,
    const console::Writer &writer
)
{
    auto &registry = *static_cast<Registry *>(context);
    if (count == 2 && std::strcmp(arguments[1], "list") == 0) {
        for (std::size_t index = 0; index < registry.count; ++index) {
            const auto &test = registry.cases[index];
            char line[128];
            std::snprintf(
                line,
                sizeof(line),
                "HWTEST CASE %s timeout_ms=%lu destructive=%u\n",
                test.name,
                static_cast<unsigned long>(test.timeout_ms),
                static_cast<unsigned>(test.destructive)
            );
            writer.Write(line);
            writer.Write("  verifies: ");
            writer.Write(test.purpose);
            writer.Write("\n");
        }
        return console::Status::kOk;
    }
    const char *selection = nullptr;
    bool destructive = false;
    if (count == 2 && std::strcmp(arguments[1], "all") == 0) {
    } else if ((count == 3 || count == 4) && std::strcmp(arguments[1], "run") == 0) {
        selection = arguments[2];
        if (!ValidName(selection) || (count == 4 && std::strcmp(arguments[3], "allow-destructive") != 0)) {
            return console::Status::kInvalidArgument;
        }
        destructive = count == 4;
    } else {
        return console::Status::kInvalidArgument;
    }
    auto destination = writer;
    Run(registry.cases,
        registry.count,
        selection,
        destructive,
        {&destination,
         [](void *target, const char *line) {
             static_cast<console::Writer *>(target)->Write(line);
             display_log::Write(line);
         }},
        registry.context,
        {nullptr,
         [](void *, const char *line) {
             display_log::Write(line);
         }});
    return console::Status::kOk;
}

}
