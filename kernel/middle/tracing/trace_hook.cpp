/*
 * trace_hook.cpp — TraceEngine global instance and trace_emit implementation
 */
#include "trace_hook.h"

namespace uai {

/* ------------------------------------------------------------------ */
/*  Global engine pointer                                              */
/* ------------------------------------------------------------------ */

static DefaultTraceEngine *g_engine = nullptr;

void trace_engine_init(DefaultTraceEngine *engine)
{
    g_engine = engine;
}

DefaultTraceEngine *trace_engine_get()
{
    return g_engine;
}

/* ------------------------------------------------------------------ */
/*  trace_emit — called by the UAI_TRACE_* macros                     */
/* ------------------------------------------------------------------ */

void trace_emit(const TraceEvent &event)
{
    if (g_engine) {
        g_engine->emit(event);
    }
}

/* ------------------------------------------------------------------ */
/*  Platform timestamp / task ID stubs (override per-platform)        */
/* ------------------------------------------------------------------ */

#if defined(UAI_PLATFORM_SIM)

#include <chrono>

static auto g_start = std::chrono::steady_clock::now();

uint32_t trace_timestamp_us()
{
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now - g_start).count());
}

uint16_t trace_current_task()
{
    return 0; /* simulation — no real tasks */
}

#elif defined(UAI_PLATFORM_STM32)

/* STM32: use DWT->CYCCNT for sub-μs precision + SysTick for coarse */
extern "C" volatile uint32_t uwTick; /* HAL tick (ms) */

/* ARM CoreSight DWT registers */
static constexpr volatile uint32_t *DWT_CTRL  = reinterpret_cast<volatile uint32_t*>(0xE0001000);
static constexpr volatile uint32_t *DWT_CYCCNT = reinterpret_cast<volatile uint32_t*>(0xE0001004);
static constexpr volatile uint32_t *CoreDebug_DEMCR = reinterpret_cast<volatile uint32_t*>(0xE000EDFC);

static uint32_t s_cpu_mhz = 0; /* set once during init */

void trace_init_dwt(uint32_t cpu_freq_hz)
{
    s_cpu_mhz = cpu_freq_hz / 1000000;
    *CoreDebug_DEMCR |= (1u << 24);  /* enable DWT */
    *DWT_CYCCNT = 0;
    *DWT_CTRL   |= 1u;               /* enable cycle counter */
}

uint32_t trace_timestamp_us()
{
    if (s_cpu_mhz > 0) {
        /* DWT cycle counter → μs (high precision) */
        return *DWT_CYCCNT / s_cpu_mhz;
    }
    /* Fallback: HAL tick × 1000 (ms → μs) */
    return uwTick * 1000;
}

/* μT-Kernel 3.0 task ID retrieval */
extern "C" int tk_get_tid(void); /* defined by μT-Kernel kernel */

uint16_t trace_current_task()
{
    return static_cast<uint16_t>(tk_get_tid());
}

#else

uint32_t trace_timestamp_us() { return 0; }
uint16_t trace_current_task() { return 0; }

#endif

} // namespace uai
