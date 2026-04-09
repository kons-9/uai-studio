/*
 * mcp_server.h — Lightweight MCP (Model Context Protocol) server for MCU
 *
 * Profile mode only: replaces the application firmware.
 * Handles JSON-RPC messages over UART / μAI-Bridge transport.
 *
 * MCP Resources (read):
 *   - rtos://tasks          Task list with state/priority/stack
 *   - rtos://semaphores     Semaphore states
 *   - rtos://memory         Memory pool statistics
 *   - rtos://probes         OTA probe measurement results
 *
 * MCP Tools (write/execute):
 *   - probe_deploy          Send & deploy probe binary
 *   - probe_call            Execute a deployed probe
 *   - probe_delete          Remove a probe
 *   - trace_attach          Attach a dynamic trace hook
 *   - trace_detach          Detach a dynamic trace hook
 *   - filter_set            Upload filter bytecode (mini-VM)
 *   - filter_clear          Remove active filter
 *
 * RAM budget: ~2–4 KB (rx/tx buffers + state).
 */
#pragma once

#include "json_parser.h"
#include "../../../tracing/firmware/trace_hook.h"
#include "../../../tracing/firmware/mini_vm/bytecode.h"
#include "../../../tracing/firmware/mini_vm/vm.h"
#include "../ota/ota_loader.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>

namespace uai {

/* ------------------------------------------------------------------ */
/*  MCP method IDs (internal dispatch)                                */
/* ------------------------------------------------------------------ */

enum class McpMethod : uint8_t {
    UNKNOWN = 0,

    /* Lifecycle */
    INITIALIZE,
    PING,

    /* Resources */
    RESOURCES_LIST,
    RESOURCES_READ,

    /* Tools */
    TOOLS_LIST,
    TOOLS_CALL,
};

/* ------------------------------------------------------------------ */
/*  Task info (populated by platform layer)                           */
/* ------------------------------------------------------------------ */

struct McpTaskInfo {
    uint16_t  id;
    uint8_t   state;       /* 0=READY, 1=RUNNING, 2=WAIT, 3=DORMANT */
    uint8_t   priority;
    uint32_t  stack_free;  /* estimated free stack bytes */
    char      name[16];
};

/* ------------------------------------------------------------------ */
/*  MCP Server                                                        */
/* ------------------------------------------------------------------ */

inline constexpr size_t MCP_RX_BUF_SIZE = 1024;
inline constexpr size_t MCP_TX_BUF_SIZE = 2048;

template <typename Transport, size_t OtaArenaSize = 4096>
class McpServer {
public:
    McpServer(Transport &transport,
              OtaLoader<OtaArenaSize> *ota = nullptr,
              DefaultTraceEngine *trace = nullptr)
        : transport_(transport)
        , ota_(ota)
        , trace_(trace)
    {}

    /* Register tasks (from platform layer) */
    int register_task(const McpTaskInfo &info)
    {
        if (n_tasks_ >= MAX_TASKS) return -1;
        tasks_[n_tasks_++] = info;
        return 0;
    }

    /* Update task state */
    int update_task(uint16_t id, uint8_t state, uint32_t stack_free)
    {
        for (size_t i = 0; i < n_tasks_; i++) {
            if (tasks_[i].id == id) {
                tasks_[i].state = state;
                tasks_[i].stack_free = stack_free;
                return 0;
            }
        }
        return -1;
    }

    /* Process incoming data. Call periodically or on data arrival. */
    int process()
    {
        /* Read from transport */
        uint8_t tmp[256];
        int n = transport_.recv(tmp, sizeof(tmp));
        if (n <= 0) return 0;

        /* Append to rx buffer */
        size_t copy = (static_cast<size_t>(n) + rx_pos_ <= MCP_RX_BUF_SIZE)
                    ? static_cast<size_t>(n)
                    : (MCP_RX_BUF_SIZE - rx_pos_);
        std::memcpy(&rx_buf_[rx_pos_], tmp, copy);
        rx_pos_ += copy;

        /* Try to parse complete JSON-RPC messages */
        return try_dispatch();
    }

private:
    static constexpr size_t MAX_TASKS = 16;

    /* Try to find and dispatch a complete JSON-RPC message */
    int try_dispatch()
    {
        /* Simple framing: look for matching braces */
        size_t start = 0;
        while (start < rx_pos_ && rx_buf_[start] != '{') start++;
        if (start >= rx_pos_) { rx_pos_ = 0; return 0; }

        int depth = 0;
        size_t end = start;
        bool in_string = false;
        for (; end < rx_pos_; end++) {
            char c = static_cast<char>(rx_buf_[end]);
            if (in_string) {
                if (c == '\\') { end++; continue; }
                if (c == '"') in_string = false;
                continue;
            }
            if (c == '"') { in_string = true; continue; }
            if (c == '{') depth++;
            if (c == '}') { depth--; if (depth == 0) { end++; break; } }
        }

        if (depth != 0) return 0; /* incomplete message */

        /* Dispatch the message */
        const char *msg = reinterpret_cast<const char *>(&rx_buf_[start]);
        size_t msg_len = end - start;
        handle_message(msg, msg_len);

        /* Shift remaining data */
        size_t remaining = rx_pos_ - end;
        if (remaining > 0) {
            std::memmove(rx_buf_, &rx_buf_[end], remaining);
        }
        rx_pos_ = remaining;

        return 1;
    }

    /* Dispatch a single JSON-RPC message */
    void handle_message(const char *json, size_t len)
    {
        JsonParser parser(json, len);

        char method[64] = {};
        if (!parser.get_method(method, sizeof(method))) {
            send_error(-1, -32600, "Invalid Request");
            return;
        }

        int32_t id = 0;
        parser.get_id(id);

        McpMethod m = resolve_method(method);

        switch (m) {
        case McpMethod::INITIALIZE:     handle_initialize(id);          break;
        case McpMethod::PING:           handle_ping(id);                break;
        case McpMethod::RESOURCES_LIST: handle_resources_list(id);      break;
        case McpMethod::RESOURCES_READ: handle_resources_read(id, json, len); break;
        case McpMethod::TOOLS_LIST:     handle_tools_list(id);          break;
        case McpMethod::TOOLS_CALL:     handle_tools_call(id, json, len); break;
        default:
            send_error(id, -32601, "Method not found");
            break;
        }
    }

    McpMethod resolve_method(const char *method)
    {
        if (std::strcmp(method, "initialize") == 0)       return McpMethod::INITIALIZE;
        if (std::strcmp(method, "ping") == 0)              return McpMethod::PING;
        if (std::strcmp(method, "resources/list") == 0)    return McpMethod::RESOURCES_LIST;
        if (std::strcmp(method, "resources/read") == 0)    return McpMethod::RESOURCES_READ;
        if (std::strcmp(method, "tools/list") == 0)        return McpMethod::TOOLS_LIST;
        if (std::strcmp(method, "tools/call") == 0)        return McpMethod::TOOLS_CALL;
        return McpMethod::UNKNOWN;
    }

    /* ---------------------------------------------------------------- */
    /*  Handler implementations                                         */
    /* ---------------------------------------------------------------- */

    void handle_initialize(int32_t id)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("result");
        w.begin_object();
          w.kv_string("protocolVersion", "2024-11-05");
          w.key("capabilities");
          w.begin_object();
            w.key("resources"); w.begin_object(); w.end_object();
            w.key("tools");     w.begin_object(); w.end_object();
          w.end_object();
          w.key("serverInfo");
          w.begin_object();
            w.kv_string("name", "uai-studio-mcu");
            w.kv_string("version", "0.1.0");
          w.end_object();
        w.end_object();
        w.end_object();
        send_response(w);
    }

    void handle_ping(int32_t id)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("result");
        w.begin_object(); w.end_object();
        w.end_object();
        send_response(w);
    }

    void handle_resources_list(int32_t id)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("result");
        w.begin_object();
          w.key("resources");
          w.begin_array();
            write_resource_desc(w, "rtos://tasks", "Task list",
                "List of RTOS tasks with state, priority, stack info");
            write_resource_desc(w, "rtos://memory", "Memory pools",
                "Memory pool usage statistics");
            write_resource_desc(w, "rtos://probes", "Probe results",
                "OTA probe measurement results");
          w.end_array();
        w.end_object();
        w.end_object();
        send_response(w);
    }

    void handle_resources_read(int32_t id, const char *json, size_t len)
    {
        JsonParser p(json, len);
        JsonValue uri_val = p.find_nested("params", "uri");
        char uri[64] = {};
        if (!uri_val.as_string(uri, sizeof(uri))) {
            send_error(id, -32602, "Missing params.uri");
            return;
        }

        if (std::strcmp(uri, "rtos://tasks") == 0) {
            read_tasks(id);
        } else if (std::strcmp(uri, "rtos://memory") == 0) {
            read_memory(id);
        } else if (std::strcmp(uri, "rtos://probes") == 0) {
            read_probes(id);
        } else {
            send_error(id, -32602, "Unknown resource URI");
        }
    }

    void handle_tools_list(int32_t id)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("result");
        w.begin_object();
          w.key("tools");
          w.begin_array();
            write_tool_desc(w, "probe_deploy",
                "Deploy a probe function binary to the probe arena");
            write_tool_desc(w, "probe_call",
                "Execute a deployed probe function");
            write_tool_desc(w, "probe_delete",
                "Remove a deployed probe from the arena");
            write_tool_desc(w, "trace_attach",
                "Attach a dynamic trace hook to a tracepoint");
            write_tool_desc(w, "trace_detach",
                "Detach a dynamic trace hook from a tracepoint");
            write_tool_desc(w, "filter_set",
                "Upload filter bytecode for the mini-VM");
            write_tool_desc(w, "filter_clear",
                "Remove the active trace filter");
          w.end_array();
        w.end_object();
        w.end_object();
        send_response(w);
    }

    void handle_tools_call(int32_t id, const char *json, size_t len)
    {
        JsonParser p(json, len);
        JsonValue name_val = p.find_nested("params", "name");
        char tool_name[32] = {};
        if (!name_val.as_string(tool_name, sizeof(tool_name))) {
            send_error(id, -32602, "Missing params.name");
            return;
        }

        if (std::strcmp(tool_name, "probe_deploy") == 0) {
            tool_probe_deploy(id, json, len);
        } else if (std::strcmp(tool_name, "probe_call") == 0) {
            tool_probe_call(id, json, len);
        } else if (std::strcmp(tool_name, "probe_delete") == 0) {
            tool_probe_delete(id, json, len);
        } else if (std::strcmp(tool_name, "trace_attach") == 0) {
            tool_trace_attach(id, json, len);
        } else if (std::strcmp(tool_name, "trace_detach") == 0) {
            tool_trace_detach(id, json, len);
        } else if (std::strcmp(tool_name, "filter_set") == 0) {
            tool_filter_set(id, json, len);
        } else if (std::strcmp(tool_name, "filter_clear") == 0) {
            tool_filter_clear(id);
        } else {
            send_error(id, -32602, "Unknown tool");
        }
    }

    /* ---- Resource read implementations ---- */

    void read_tasks(int32_t id)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("result");
        w.begin_object();
          w.key("contents");
          w.begin_array();
            /* Single text content with task table */
            w.begin_object();
            w.kv_string("uri", "rtos://tasks");
            w.kv_string("mimeType", "application/json");
            w.key("text");

            /* Build task JSON as string value */
            char task_json[1024];
            JsonWriter tw(task_json, sizeof(task_json));
            tw.begin_array();
            for (size_t i = 0; i < n_tasks_; i++) {
                if (i > 0) { /* array_element handled by JsonWriter */ }
                tw.begin_object();
                tw.kv_uint("id", tasks_[i].id);
                tw.kv_string("name", tasks_[i].name);
                tw.kv_uint("state", tasks_[i].state);
                tw.kv_uint("priority", tasks_[i].priority);
                tw.kv_uint("stack_free", tasks_[i].stack_free);
                tw.end_object();
            }
            tw.end_array();
            w.value_string(task_json);
            w.end_object();
          w.end_array();
        w.end_object();
        w.end_object();
        send_response(w);
    }

    void read_memory(int32_t id)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("result");
        w.begin_object();
          w.key("contents");
          w.begin_array();
            w.begin_object();
            w.kv_string("uri", "rtos://memory");
            w.kv_string("mimeType", "application/json");
            w.key("text");

            char mem_json[512];
            JsonWriter mw(mem_json, sizeof(mem_json));
            mw.begin_object();
            if (ota_) {
                auto &arena = ota_->arena();
                mw.kv_uint("probe_arena_total", static_cast<uint32_t>(OtaArenaSize));
                mw.kv_uint("probe_arena_used", static_cast<uint32_t>(arena.arena_used()));
                mw.kv_uint("probe_slots_active", arena.active_count());
                mw.kv_uint("probe_slots_max", static_cast<uint32_t>(MAX_PROBES));
            }
            mw.kv_uint("task_count", static_cast<uint32_t>(n_tasks_));
            mw.kv_uint("mcp_rx_buf", static_cast<uint32_t>(MCP_RX_BUF_SIZE));
            mw.kv_uint("mcp_tx_buf", static_cast<uint32_t>(MCP_TX_BUF_SIZE));
            mw.end_object();
            w.value_string(mem_json);

            w.end_object();
          w.end_array();
        w.end_object();
        w.end_object();
        send_response(w);
    }

    void read_probes(int32_t id)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("result");
        w.begin_object();
          w.key("contents");
          w.begin_array();
            w.begin_object();
            w.kv_string("uri", "rtos://probes");
            w.kv_string("mimeType", "application/json");
            w.key("text");

            char probes_json[1024];
            JsonWriter pw(probes_json, sizeof(probes_json));
            pw.begin_array();
            if (ota_) {
                for (size_t i = 0; i < MAX_PROBES; i++) {
                    const auto *slot = ota_->get_results(static_cast<int>(i));
                    if (!slot || !slot->active) continue;
                    pw.begin_object();
                    pw.kv_uint("slot", static_cast<uint32_t>(i));
                    pw.kv_string("name", slot->name);
                    pw.kv_uint("code_size", slot->code_size);
                    pw.kv_uint("call_count", slot->call_count);
                    pw.kv_uint("total_us", slot->total_us);
                    pw.kv_uint("min_us", slot->call_count > 0 ? slot->min_us : 0);
                    pw.kv_uint("max_us", slot->max_us);
                    if (slot->call_count > 0) {
                        pw.kv_uint("avg_us", slot->total_us / slot->call_count);
                    }
                    pw.end_object();
                }
            }
            pw.end_array();
            w.value_string(probes_json);

            w.end_object();
          w.end_array();
        w.end_object();
        w.end_object();
        send_response(w);
    }

    /* ---- Tool call implementations ---- */

    void tool_probe_deploy(int32_t id, const char *json, size_t len)
    {
        if (!ota_) {
            send_tool_result(id, true, "OTA loader not available");
            return;
        }

        JsonParser p(json, len);

        /* Extract params.arguments.name */
        char probe_name[16] = {};
        JsonValue name_val = p.find_nested3("params", "arguments", "name");
        if (!name_val.as_string(probe_name, sizeof(probe_name))) {
            send_tool_result(id, true, "Missing arguments.name");
            return;
        }

        /* Extract params.arguments.binary_b64 */
        JsonParser p2(json, len);
        char b64_buf[1400] = {};
        JsonValue b64_val = p2.find_nested3("params", "arguments", "binary_b64");
        if (!b64_val.as_string(b64_buf, sizeof(b64_buf))) {
            send_tool_result(id, true, "Missing arguments.binary_b64");
            return;
        }

        int slot = ota_->deploy_b64(probe_name, b64_buf, std::strlen(b64_buf));
        if (slot < 0) {
            char err[64];
            JsonWriter ew(err, sizeof(err));
            /* Manual format: "Deploy failed (error: -N)" */
            std::snprintf(err, sizeof(err), "Deploy failed (error: %d)", slot);
            send_tool_result(id, true, err);
            return;
        }

        char msg[64];
        std::snprintf(msg, sizeof(msg),
                      "Probe '%s' deployed to slot %d", probe_name, slot);
        send_tool_result(id, false, msg);
    }

    void tool_probe_call(int32_t id, const char *json, size_t len)
    {
        if (!ota_) {
            send_tool_result(id, true, "OTA loader not available");
            return;
        }

        JsonParser p(json, len);

        /* Try name first, then slot index */
        char probe_name[16] = {};
        JsonValue name_val = p.find_nested3("params", "arguments", "name");
        uint32_t elapsed_us = 0;
        int rc;

        if (name_val.as_string(probe_name, sizeof(probe_name))) {
            rc = ota_->execute_by_name(probe_name, elapsed_us);
        } else {
            JsonParser p2(json, len);
            int32_t slot_idx = -1;
            JsonValue slot_val = p2.find_nested3("params", "arguments", "slot");
            if (!slot_val.as_int(slot_idx)) {
                send_tool_result(id, true, "Missing arguments.name or arguments.slot");
                return;
            }
            rc = ota_->execute(slot_idx, elapsed_us);
        }

        if (rc < 0) {
            char err[64];
            std::snprintf(err, sizeof(err), "Probe call failed (error: %d)", rc);
            send_tool_result(id, true, err);
            return;
        }

        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "Probe executed: %u us", static_cast<unsigned>(elapsed_us));
        send_tool_result(id, false, msg);
    }

    void tool_probe_delete(int32_t id, const char *json, size_t len)
    {
        if (!ota_) {
            send_tool_result(id, true, "OTA loader not available");
            return;
        }

        JsonParser p(json, len);
        char probe_name[16] = {};
        JsonValue name_val = p.find_nested3("params", "arguments", "name");
        int rc;

        if (name_val.as_string(probe_name, sizeof(probe_name))) {
            rc = ota_->remove_by_name(probe_name);
        } else {
            JsonParser p2(json, len);
            int32_t slot_idx = -1;
            JsonValue slot_val = p2.find_nested3("params", "arguments", "slot");
            if (!slot_val.as_int(slot_idx)) {
                send_tool_result(id, true, "Missing arguments.name or arguments.slot");
                return;
            }
            rc = ota_->remove(slot_idx);
        }

        if (rc < 0) {
            char err[64];
            std::snprintf(err, sizeof(err), "Delete failed (error: %d)", rc);
            send_tool_result(id, true, err);
            return;
        }

        send_tool_result(id, false, "Probe deleted");
    }

    void tool_trace_attach(int32_t id, const char *json, size_t len)
    {
#ifdef UAI_TRACE_DYNAMIC
        if (!trace_) {
            send_tool_result(id, true, "Trace engine not available");
            return;
        }

        JsonParser p(json, len);
        char tp_name[32] = {};
        JsonValue tp_val = p.find_nested3("params", "arguments", "tracepoint");
        if (!tp_val.as_string(tp_name, sizeof(tp_name))) {
            send_tool_result(id, true, "Missing arguments.tracepoint");
            return;
        }

        TracepointId tp = resolve_tracepoint(tp_name);
        if (tp == TracepointId::_COUNT) {
            send_tool_result(id, true, "Unknown tracepoint");
            return;
        }

        /* Attach a hook that records to the ring buffer (emit is default).
         * In practice this enables event flow for the specified tracepoint. */
        int rc = trace_->attach(tp, [](const TraceEvent &ev) {
            /* Hook body intentionally empty — events are already buffered
             * by TraceEngine::emit(). Attach merely enables the hook slot
             * for future use (e.g. counting, conditional breakpoints). */
            (void)ev;
        });

        if (rc < 0) {
            send_tool_result(id, true, "Attach failed");
            return;
        }

        char msg[64];
        std::snprintf(msg, sizeof(msg), "Trace hook attached: %s", tp_name);
        send_tool_result(id, false, msg);
#else
        (void)json; (void)len;
        send_tool_result(id, true, "Dynamic tracing not enabled");
#endif
    }

    void tool_trace_detach(int32_t id, const char *json, size_t len)
    {
#ifdef UAI_TRACE_DYNAMIC
        if (!trace_) {
            send_tool_result(id, true, "Trace engine not available");
            return;
        }

        JsonParser p(json, len);
        char tp_name[32] = {};
        JsonValue tp_val = p.find_nested3("params", "arguments", "tracepoint");

        if (tp_val.as_string(tp_name, sizeof(tp_name))) {
            TracepointId tp = resolve_tracepoint(tp_name);
            if (tp == TracepointId::_COUNT) {
                send_tool_result(id, true, "Unknown tracepoint");
                return;
            }
            trace_->detach(tp);
            char msg[64];
            std::snprintf(msg, sizeof(msg), "Trace hook detached: %s", tp_name);
            send_tool_result(id, false, msg);
        } else {
            /* No tracepoint specified → detach all */
            trace_->detach_all();
            send_tool_result(id, false, "All trace hooks detached");
        }
#else
        (void)json; (void)len;
        send_tool_result(id, true, "Dynamic tracing not enabled");
#endif
    }

    /* Resolve tracepoint name string to TracepointId */
    static TracepointId resolve_tracepoint(const char *name)
    {
        if (std::strcmp(name, "TASK_SWITCH") == 0)    return TracepointId::TASK_SWITCH;
        if (std::strcmp(name, "TASK_READY") == 0)     return TracepointId::TASK_READY;
        if (std::strcmp(name, "TASK_WAIT") == 0)      return TracepointId::TASK_WAIT;
        if (std::strcmp(name, "TASK_DORMANT") == 0)   return TracepointId::TASK_DORMANT;
        if (std::strcmp(name, "SEM_SIGNAL") == 0)     return TracepointId::SEM_SIGNAL;
        if (std::strcmp(name, "SEM_WAIT") == 0)       return TracepointId::SEM_WAIT;
        if (std::strcmp(name, "MTX_LOCK") == 0)       return TracepointId::MTX_LOCK;
        if (std::strcmp(name, "MTX_UNLOCK") == 0)     return TracepointId::MTX_UNLOCK;
        if (std::strcmp(name, "MEM_ALLOC") == 0)      return TracepointId::MEM_ALLOC;
        if (std::strcmp(name, "MEM_FREE") == 0)       return TracepointId::MEM_FREE;
        if (std::strcmp(name, "MEM_CORRUPTION") == 0) return TracepointId::MEM_CORRUPTION;
        if (std::strcmp(name, "IRQ_ENTER") == 0)      return TracepointId::IRQ_ENTER;
        if (std::strcmp(name, "IRQ_EXIT") == 0)       return TracepointId::IRQ_EXIT;
        if (std::strcmp(name, "USER_EVENT") == 0)     return TracepointId::USER_EVENT;
        return TracepointId::_COUNT; /* unknown */
    }

    void tool_filter_set(int32_t id, const char *json, size_t len)
    {
#ifdef UAI_TRACE_DYNAMIC
        if (!trace_) {
            send_tool_result(id, true, "Trace engine not available");
            return;
        }

        JsonParser p(json, len);
        char b64_buf[512] = {};
        JsonValue b64_val = p.find_nested3("params", "arguments", "bytecode_b64");
        if (!b64_val.as_string(b64_buf, sizeof(b64_buf))) {
            send_tool_result(id, true, "Missing arguments.bytecode_b64");
            return;
        }

        /* Decode Base64 bytecode into VmProgram */
        uint8_t decoded[VM_MAX_PROGRAM];
        size_t decoded_len = detail::b64_decode(
            b64_buf, std::strlen(b64_buf), decoded, sizeof(decoded));

        if (decoded_len == 0 || decoded_len > VM_MAX_PROGRAM) {
            send_tool_result(id, true, "Invalid bytecode");
            return;
        }

        /* Store the program and set a VM-based filter on the trace engine */
        std::memcpy(vm_program_.code, decoded, decoded_len);
        vm_program_.length = static_cast<uint16_t>(decoded_len);

        s_vm_program_ = &vm_program_;
        trace_->set_filter(vm_filter_dispatch);

        char msg[64];
        std::snprintf(msg, sizeof(msg),
                      "Filter set (%u bytes bytecode)",
                      static_cast<unsigned>(decoded_len));
        send_tool_result(id, false, msg);
#else
        (void)json; (void)len;
        send_tool_result(id, true, "Dynamic tracing not enabled");
#endif
    }

    void tool_filter_clear(int32_t id)
    {
#ifdef UAI_TRACE_DYNAMIC
        if (trace_) {
            trace_->clear_filter();
            s_vm_program_ = nullptr;
            send_tool_result(id, false, "Filter cleared");
            return;
        }
#endif
        send_tool_result(id, true, "Trace engine not available");
    }

    /* ---- Response helpers ---- */

    void write_resource_desc(JsonWriter &w, const char *uri,
                             const char *name, const char *desc)
    {
        w.begin_object();
        w.kv_string("uri", uri);
        w.kv_string("name", name);
        w.kv_string("description", desc);
        w.end_object();
    }

    void write_tool_desc(JsonWriter &w, const char *name, const char *desc)
    {
        w.begin_object();
        w.kv_string("name", name);
        w.kv_string("description", desc);
        w.key("inputSchema");
        w.begin_object();
          w.kv_string("type", "object");
        w.end_object();
        w.end_object();
    }

    void send_tool_result(int32_t id, bool is_error, const char *text)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("result");
        w.begin_object();
          w.key("content");
          w.begin_array();
            w.begin_object();
            w.kv_string("type", "text");
            w.kv_string("text", text);
            w.end_object();
          w.end_array();
          if (is_error) { w.kv_bool("isError", true); }
        w.end_object();
        w.end_object();
        send_response(w);
    }

    void send_error(int32_t id, int code, const char *message)
    {
        JsonWriter w(tx_buf_, MCP_TX_BUF_SIZE);
        w.begin_object();
        w.kv_string("jsonrpc", "2.0");
        w.kv_int("id", id);
        w.key("error");
        w.begin_object();
          w.kv_int("code", code);
          w.kv_string("message", message);
        w.end_object();
        w.end_object();
        send_response(w);
    }

    void send_response(const JsonWriter &w)
    {
        transport_.send(reinterpret_cast<const uint8_t *>(w.c_str()),
                        w.length());
    }

    /* ---- State ---- */

    Transport           &transport_;
    OtaLoader<OtaArenaSize> *ota_;
    DefaultTraceEngine  *trace_;

    McpTaskInfo  tasks_[MAX_TASKS]{};
    size_t       n_tasks_ = 0;

    uint8_t rx_buf_[MCP_RX_BUF_SIZE]{};
    size_t  rx_pos_ = 0;
    char    tx_buf_[MCP_TX_BUF_SIZE]{};

    /* VM filter program (stored for lifetime of filter) */
    VmProgram vm_program_{};

    /* Static filter dispatch — runs VM on each trace event.
     * Uses a global pointer because TraceEngine::FilterFn is a plain
     * function pointer (no captures). Only one McpServer should exist. */
    static inline VmProgram *s_vm_program_ = nullptr;

    static bool vm_filter_dispatch(const TraceEvent &event)
    {
        if (!s_vm_program_ || s_vm_program_->length == 0) return true;
        MiniVm vm;
        auto result = vm.execute(*s_vm_program_, event);
        if (result.error != VmError::OK) return true; /* pass on error */
        return result.pass;
    }
};

} // namespace uai
