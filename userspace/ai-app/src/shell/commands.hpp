#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "shell/engine.hpp"
#include "shell/mailbox.hpp"

namespace uai::ai::shell {

struct Context {
    Engine &engine;
    Mailbox &mailbox;
    std::uint32_t (*now)() = nullptr;
    void (*wait_ms)(std::uint32_t) = nullptr;
    void (*list_tasks)(const Output &) = nullptr;
    void (*memory_usage)(const Output &) = nullptr;
    void (*transfer_trace)(const Output &, bool) = nullptr;

    void Send(const Request &request, const Output &output) const
    {
        Reply late{};
        if (mailbox.Receive(&late)) {
            output.Write("previous command: ");
            if (late.code != 0)
                output.Printf("error code=%ld\r\n", static_cast<long>(late.code));
            else
                output.Write(late.text);
        }
        if (!mailbox.Post(request)) {
            output.Write("error: command pending\r\n");
            return;
        }
        for (std::uint32_t elapsed = 0; elapsed < 1000U; ++elapsed) {
            Reply reply{};
            if (mailbox.Receive(&reply)) {
                if (reply.code != 0)
                    output.Printf("error: apply code=%ld\r\n", static_cast<long>(reply.code));
                else
                    output.Write(reply.text);
                return;
            }
            wait_ms(1);
        }
        output.Write("error: response timeout (request may still apply)\r\n");
    }
};

inline bool ParseInt(const char *text, std::int32_t minimum, std::int32_t maximum, std::int32_t *value)
{
    if (text == nullptr || *text == '\0')
        return false;
    errno = 0;
    char *end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed < minimum || parsed > maximum)
        return false;
    *value = static_cast<std::int32_t>(parsed);
    return true;
}

inline bool ParseOnOff(const char *text, std::int32_t *value)
{
    if (std::strcmp(text, "on") == 0) {
        *value = 1;
        return true;
    }
    if (std::strcmp(text, "off") == 0) {
        *value = 0;
        return true;
    }
    return false;
}

bool RegisterHelp(Engine &engine, Context &context);
bool RegisterUptime(Engine &engine, Context &context);
bool RegisterTasks(Engine &engine, Context &context);
bool RegisterMemory(Engine &engine, Context &context);
bool RegisterLog(Engine &engine, Context &context);
bool RegisterCamera(Engine &engine, Context &context);
bool RegisterModels(Engine &engine, Context &context);
bool RegisterUi(Engine &engine, Context &context);
bool RegisterDiagnostics(Engine &engine, Context &context);
bool RegisterTrace(Engine &engine, Context &context);

inline bool RegisterAll(Engine &engine, Context &context)
{
    return RegisterHelp(engine, context) && RegisterUptime(engine, context) && RegisterTasks(engine, context)
        && RegisterMemory(engine, context) && RegisterLog(engine, context) && RegisterCamera(engine, context)
        && RegisterModels(engine, context) && RegisterUi(engine, context)
        && RegisterDiagnostics(engine, context) && RegisterTrace(engine, context);
}

} // namespace uai::ai::shell