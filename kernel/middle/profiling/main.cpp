/*
 * main.cpp — μAI-Studio profile mode firmware entry point
 *
 * This firmware replaces the application on the MCU.
 * It runs the MCP server, OTA loader, and trace engine together,
 * allowing the PC-side AI agent to query RTOS state and deploy
 * probe functions interactively.
 *
 * Build with:
 *   cmake -DUAI_PLATFORM_SIM=ON ..    (for PC simulation)
 *   cmake -DUAI_PLATFORM_STM32=ON ..  (for STM32 target)
 */

#include "../../tracing/firmware/tracepoint.h"
#include "../../tracing/firmware/trace_hook.h"
#include "../../tracing/firmware/trace_sender.h"
#include "../../tracing/firmware/stack_monitor.h"
#include "../../tracing/firmware/mini_vm/bytecode.h"
#include "../../tracing/firmware/mini_vm/vm.h"
#include "mcp/json_parser.h"
#include "mcp/mcp_server.h"
#include "ota/probe_arena.h"
#include "ota/ota_loader.h"

#include <cstdio>
#include <cstdint>

/* ------------------------------------------------------------------ */
/*  Platform-specific transport                                        */
/* ------------------------------------------------------------------ */

#if defined(UAI_PLATFORM_SIM)

#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  #define INVALID_SOCK INVALID_SOCKET
  #define CLOSE_SOCKET closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  using socket_t = int;
  #define INVALID_SOCK (-1)
  #define CLOSE_SOCKET close
#endif

/**
 * TCP transport for simulation mode.
 * Listens on a TCP port and handles one client at a time.
 */
class TcpSimTransport {
public:
    int listen(uint16_t port)
    {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2,2), &wsa);
#endif
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ == INVALID_SOCK) return -1;

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&opt), sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr),
                 sizeof(addr)) < 0)
            return -2;

        if (::listen(listen_fd_, 1) < 0) return -3;

        std::printf("[MCP] Listening on port %u...\n", port);
        return 0;
    }

    int accept_client()
    {
        struct sockaddr_in client_addr{};
        int addrlen = sizeof(client_addr);
        client_fd_ = accept(listen_fd_,
            reinterpret_cast<struct sockaddr *>(&client_addr),
            reinterpret_cast<socklen_t *>(&addrlen));
        if (client_fd_ == INVALID_SOCK) return -1;
        connected_ = true;
        std::printf("[MCP] Client connected.\n");
        return 0;
    }

    int send(const uint8_t *data, size_t len)
    {
        if (!connected_) return -1;
        int sent = ::send(client_fd_, reinterpret_cast<const char *>(data),
                          static_cast<int>(len), 0);
        return sent;
    }

    int recv(uint8_t *buf, size_t len)
    {
        if (!connected_) return -1;
        int n = ::recv(client_fd_, reinterpret_cast<char *>(buf),
                       static_cast<int>(len), 0);
        if (n <= 0) { connected_ = false; }
        return n;
    }

    bool is_connected() const { return connected_; }

    void close_client()
    {
        if (client_fd_ != INVALID_SOCK) {
            CLOSE_SOCKET(client_fd_);
            client_fd_ = INVALID_SOCK;
        }
        connected_ = false;
    }

    void shutdown()
    {
        close_client();
        if (listen_fd_ != INVALID_SOCK) {
            CLOSE_SOCKET(listen_fd_);
            listen_fd_ = INVALID_SOCK;
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

private:
    socket_t listen_fd_ = INVALID_SOCK;
    socket_t client_fd_ = INVALID_SOCK;
    bool     connected_ = false;
};

#endif /* UAI_PLATFORM_SIM */

/* ------------------------------------------------------------------ */
/*  Firmware subsystems                                                */
/* ------------------------------------------------------------------ */

namespace {

/* Trace engine (medium ring buffer — 256 events) */
uai::DefaultTraceEngine g_trace_engine({/*.dynamic_enabled=*/true});

/* OTA loader (4 KB probe arena) */
uai::DefaultOtaLoader g_ota_loader;

/* Stack canary monitor */
uai::StackMonitor<> g_stack_monitor;

/* Demo task table */
struct DemoTask {
    uint16_t id;
    const char *name;
    uint8_t  priority;
    uint8_t  state;       /* 0=READY,1=RUN,2=WAIT,3=DORM */
    uint32_t stack_free;
};

DemoTask g_demo_tasks[] = {
    { 1, "idle_task",    0,  0, 2048 },
    { 2, "mcp_server",   5,  1,  896 },
    { 3, "trace_send",   4,  2, 1024 },
    { 4, "watchdog",     7,  0,  512 },
};
constexpr size_t N_DEMO_TASKS = sizeof(g_demo_tasks) / sizeof(g_demo_tasks[0]);

} // namespace

/* ------------------------------------------------------------------ */
/*  Profile mode main loop                                             */
/* ------------------------------------------------------------------ */

#if defined(UAI_PLATFORM_SIM)

static void register_demo_tasks(uai::McpServer<TcpSimTransport> &server)
{
    for (size_t i = 0; i < N_DEMO_TASKS; i++) {
        uai::McpTaskInfo info{};
        info.id = g_demo_tasks[i].id;
        info.state = g_demo_tasks[i].state;
        info.priority = g_demo_tasks[i].priority;
        info.stack_free = g_demo_tasks[i].stack_free;
        std::strncpy(info.name, g_demo_tasks[i].name, sizeof(info.name) - 1);
        server.register_task(info);
    }
}

int main(int argc, char *argv[])
{
    uint16_t port = 5555;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    std::printf("╔══════════════════════════════════════════╗\n");
    std::printf("║  μAI-Studio Profile Mode (Simulation)    ║\n");
    std::printf("╚══════════════════════════════════════════╝\n\n");

    /* Initialize trace engine */
    uai::trace_engine_init(&g_trace_engine);

    /* Set up transport */
    TcpSimTransport transport;
    if (transport.listen(port) < 0) {
        std::fprintf(stderr, "[ERROR] Failed to listen on port %u\n", port);
        return 1;
    }

    /* Main server loop — accept clients and handle MCP messages */
    while (true) {
        std::printf("[MCP] Waiting for client...\n");

        if (transport.accept_client() < 0) {
            std::fprintf(stderr, "[ERROR] Failed to accept client\n");
            continue;
        }

        /* Create MCP server for this connection */
        uai::McpServer<TcpSimTransport> server(
            transport, &g_ota_loader, &g_trace_engine);
        register_demo_tasks(server);

        std::printf("[MCP] Server ready. Processing requests...\n");

        /* Process messages until client disconnects */
        while (transport.is_connected()) {
            int processed = server.process();
            if (processed < 0) break;

            /* Check stack canaries periodically */
            g_stack_monitor.check_all(uai::trace_emit);

            /* Small delay to avoid busy-loop */
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        transport.close_client();
        std::printf("[MCP] Client disconnected.\n\n");
    }

    transport.shutdown();
    return 0;
}

#else /* Real MCU target (STM32 / μT-Kernel) */

/*
 * On real hardware, main() is provided by the μT-Kernel startup code.
 * The MCP server runs as a task (usermain → cre_tsk + sta_tsk).
 * Transport uses UART or μAI-Bridge TCP over the network stack.
 *
 * Skeleton:
 *
 *   #include <tk/tkernel.h>
 *
 *   void mcp_task(INT stacd, void *exinf)
 *   {
 *       UartTransport transport;
 *       transport.init(UART_CH, BAUD_115200);
 *
 *       McpServer<UartTransport> server(transport, &g_ota_loader, &g_trace_engine);
 *       // ... register tasks from tk_ref_tsk() ...
 *
 *       while (true) {
 *           server.process();
 *           g_stack_monitor.check_all(trace_emit);
 *           tk_dly_tsk(10);
 *       }
 *   }
 *
 *   EXPORT INT usermain(void)
 *   {
 *       trace_engine_init(&g_trace_engine);
 *
 *       T_CTSK ctsk = {};
 *       ctsk.tskatr = TA_HLNG | TA_RNG0;
 *       ctsk.task = mcp_task;
 *       ctsk.itskpri = 5;
 *       ctsk.stksz = 4096;
 *       ID tsk = tk_cre_tsk(&ctsk);
 *       tk_sta_tsk(tsk, 0);
 *
 *       return 0;  // μT-Kernel scheduler takes over
 *   }
 */

#endif /* UAI_PLATFORM_SIM */
