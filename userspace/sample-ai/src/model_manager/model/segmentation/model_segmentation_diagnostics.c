#include "model_manager/model/segmentation/model_segmentation_diagnostics.h"

#include "driver/npu_driver/debug.h"
#include "ll_aton_NN_interface.h"
#include "ll_aton_caches_interface.h"
#include "ll_aton_platform.h"
#include <tm/tmonitor.h>

#include <stdbool.h>
#include <stdint.h>

enum {
    kEpochRecordCapacity = 128U,
    kIrqRecordCapacity = 128U,
    kPollRecordCapacity = 24U,
    kPollIntervalTicks = 250U,
    kResize194OutputAddress = 0x342c4600U,
    kResize194OutputSize = 102400U,
    kResize194OutputFill = 0x80U,
    kResize194PostGuardAddress = 0x342dd640U,
    kResize194PostGuardSize = 64U,
    kResize194PostGuardPattern = 0xa5U,
};

enum {
    kGuardInitialized = 1U << 0,
    kGuardCheckedAfterResize = 1U << 1,
    kGuardIntactAfterResize = 1U << 2,
    kGuardCheckedAtEpoch54Start = 1U << 3,
    kGuardIntactAtEpoch54Start = 1U << 4,
    kGuardChangedDuringPolling = 1U << 5,
};

typedef struct {
    volatile uint32_t committed;
    uint32_t sequence;
    uint32_t event;
    int32_t epoch;
    int32_t last_epoch;
    uint32_t flags;
    uint32_t wait_mask;
    uint32_t blob_address;
    uint32_t irq_count;
    uint32_t last_irqs;
    uint32_t epoch_control;
    uint32_t epoch_address;
    uint32_t epoch_irq;
    uint32_t interrupt_status;
    uint32_t bus_error;
} EpochRecord;

typedef struct {
    volatile uint32_t committed;
    uint32_t sequence;
    uint32_t edge;
    uint32_t irq_count;
    uint32_t last_irqs;
    uint32_t epoch_control;
    uint32_t epoch_address;
    uint32_t epoch_irq;
    uint32_t interrupt_status;
    uint32_t bus_error;
} IrqRecord;

typedef struct {
    volatile uint32_t committed;
    uint32_t tick;
    uint32_t stai_status;
    uint32_t irq_count;
    uint32_t last_irqs;
    uint32_t epoch_control;
    uint32_t epoch_address;
    uint32_t epoch_irq;
    uint32_t interrupt_status;
    uint32_t bus_error;
} PollRecord;

static volatile EpochRecord s_epoch_records[kEpochRecordCapacity];
static volatile IrqRecord s_irq_records[kIrqRecordCapacity];
static volatile PollRecord s_poll_records[kPollRecordCapacity];
static volatile uint32_t s_epoch_total;
static volatile uint32_t s_irq_total;
static volatile uint32_t s_poll_total;
static volatile bool s_recording;
static bool s_first_run_claimed;
static bool s_resize194_output_replaced;
static volatile uint32_t s_resize194_post_guard_state;
static UB s_diag_line[256];

/* T-Monitor tm_printf() sends one character at a time, so concurrent camera
 * task output can splice characters into diagnostic records. tm_putstring()
 * holds the T-Monitor interrupt lock for the complete line. */
#define SEGDIAG_PRINT(...)          \
    do {                            \
        (void)tm_sprintf(s_diag_line, __VA_ARGS__); \
        tm_putstring(s_diag_line);  \
    } while (0)

static void InitializeResize194PostGuard(void)
{
    volatile uint8_t *const guard =
        (volatile uint8_t *)(uintptr_t)kResize194PostGuardAddress;
    for (uint32_t i = 0U; i < kResize194PostGuardSize; ++i) {
        guard[i] = (uint8_t)kResize194PostGuardPattern;
    }
    LL_ATON_Cache_MCU_Clean_Range((uintptr_t)guard,
                                 kResize194PostGuardSize);
    s_resize194_post_guard_state = kGuardInitialized;
}

static bool Resize194PostGuardIsIntact(void)
{
    const volatile uint8_t *const guard =
        (const volatile uint8_t *)(uintptr_t)kResize194PostGuardAddress;
    for (uint32_t i = 0U; i < kResize194PostGuardSize; ++i) {
        if (guard[i] != (uint8_t)kResize194PostGuardPattern) {
            return false;
        }
    }
    return true;
}

static void RecordHardware(volatile uint32_t *irq_count,
                           volatile uint32_t *last_irqs,
                           volatile uint32_t *epoch_control,
                           volatile uint32_t *epoch_address,
                           volatile uint32_t *epoch_irq,
                           volatile uint32_t *interrupt_status,
                           volatile uint32_t *bus_error)
{
    *irq_count = g_aton_irq_count;
    *last_irqs = g_aton_last_irqs;
    *epoch_control = ATON_EPOCHCTRL_CTRL_GET(0U);
    *epoch_address = ATON_EPOCHCTRL_ADDR_GET(0U);
    *epoch_irq = ATON_EPOCHCTRL_IRQ_GET(0U);
    *interrupt_status = ATON_INTCTRL_INTREG_GET(0U);
    *bus_error = ATON_BUSIF_ERR_GET(0U);
}

int ai_segmentation_diag_begin(void)
{
    if (s_first_run_claimed) {
        return 0;
    }

    s_first_run_claimed = true;
    s_epoch_total = 0U;
    s_irq_total = 0U;
    s_poll_total = 0U;
    s_resize194_output_replaced = false;
    s_resize194_post_guard_state = 0U;
    s_recording = true;
    return 1;
}

void ai_segmentation_diag_epoch_callback(void *cookie,
                                         stai_event_type event,
                                         const void *payload)
{
    (void)cookie;
    if (!s_recording || payload == NULL) {
        return;
    }

    const LL_ATON_RT_EpochBlockItem_t *block =
        (const LL_ATON_RT_EpochBlockItem_t *)payload;

#if defined(LL_ATON_EB_DBG_INFO)
    if (event == LL_ATON_RT_Callbacktype_PRE_START &&
        block->epoch_num == 53) {
        InitializeResize194PostGuard();
    }

    if (event == LL_ATON_RT_Callbacktype_POST_END &&
        block->epoch_num == 53 &&
        (s_resize194_post_guard_state & kGuardInitialized) != 0U) {
        s_resize194_post_guard_state |= kGuardCheckedAfterResize;
        if (Resize194PostGuardIsIntact()) {
            s_resize194_post_guard_state |= kGuardIntactAfterResize;
        }
    }

    if (event == LL_ATON_RT_Callbacktype_PRE_START &&
        block->epoch_num == 54 &&
        (s_resize194_post_guard_state & kGuardInitialized) != 0U) {
        s_resize194_post_guard_state |= kGuardCheckedAtEpoch54Start;
        if (Resize194PostGuardIsIntact()) {
            s_resize194_post_guard_state |= kGuardIntactAtEpoch54Start;
        }
    }

    if (!s_resize194_output_replaced &&
        event == LL_ATON_RT_Callbacktype_POST_END &&
        block->epoch_num == 53) {
        volatile uint8_t *const output =
            (volatile uint8_t *)(uintptr_t)kResize194OutputAddress;
        for (uint32_t i = 0U; i < kResize194OutputSize; ++i) {
            output[i] = (uint8_t)kResize194OutputFill;
        }
        LL_ATON_Cache_MCU_Clean_Range((uintptr_t)output,
                                     kResize194OutputSize);
        s_resize194_output_replaced = true;
        SEGDIAG_PRINT((const UB *)
                          "segdiag: epoch53 output fill addr=%x size=%u value=%x clean=done\n",
                      (unsigned int)kResize194OutputAddress,
                      (unsigned int)kResize194OutputSize,
                      (unsigned int)kResize194OutputFill);
    }
#endif

    const uint32_t sequence = s_epoch_total++;
    volatile EpochRecord *record =
        &s_epoch_records[sequence % kEpochRecordCapacity];
    record->committed = 0U;
    record->sequence = sequence;
    record->event = (uint32_t)event;
#if defined(LL_ATON_EB_DBG_INFO)
    record->epoch = block->epoch_num;
    record->last_epoch = block->last_epoch_num;
#else
    record->epoch = -1;
    record->last_epoch = -1;
#endif
    record->flags = block->flags;
    record->wait_mask = block->wait_mask;
    record->blob_address = (uint32_t)block->blob_address;
    RecordHardware(&record->irq_count, &record->last_irqs,
                   &record->epoch_control, &record->epoch_address,
                   &record->epoch_irq, &record->interrupt_status,
                   &record->bus_error);
    record->committed = sequence + 1U;
}

void ai_segmentation_diag_irq(uint32_t edge)
{
    if (!s_recording) {
        return;
    }

    const uint32_t sequence = s_irq_total++;
    volatile IrqRecord *record =
        &s_irq_records[sequence % kIrqRecordCapacity];
    record->committed = 0U;
    record->sequence = sequence;
    record->edge = edge;
    RecordHardware(&record->irq_count, &record->last_irqs,
                   &record->epoch_control, &record->epoch_address,
                   &record->epoch_irq, &record->interrupt_status,
                   &record->bus_error);
    record->committed = sequence + 1U;
}

void ai_segmentation_diag_poll(uint32_t tick, uint32_t stai_status)
{
    if (!s_recording || (tick % kPollIntervalTicks) != 0U) {
        return;
    }

    if ((s_resize194_post_guard_state & kGuardInitialized) != 0U &&
        !Resize194PostGuardIsIntact()) {
        s_resize194_post_guard_state |= kGuardChangedDuringPolling;
    }

    const uint32_t sequence = s_poll_total++;
    volatile PollRecord *record =
        &s_poll_records[sequence % kPollRecordCapacity];

    record->committed = 0U;
    record->tick = tick;
    record->stai_status = stai_status;
    RecordHardware(&record->irq_count, &record->last_irqs,
                   &record->epoch_control, &record->epoch_address,
                   &record->epoch_irq, &record->interrupt_status,
                   &record->bus_error);
    record->committed = sequence + 1U;
}

void ai_segmentation_diag_dump(void)
{
    s_recording = false;

    SEGDIAG_PRINT((const UB *)
                      "segdiag: summary epoch=%u irq=%u poll=%u resize194_fill=%u guard=%x final=%x/%x/%x/%x\n",
                  (unsigned int)s_epoch_total, (unsigned int)s_irq_total,
                  (unsigned int)s_poll_total,
                  (unsigned int)s_resize194_output_replaced,
                  (unsigned int)s_resize194_post_guard_state,
                  (unsigned int)ATON_EPOCHCTRL_CTRL_GET(0U),
                  (unsigned int)ATON_EPOCHCTRL_IRQ_GET(0U),
                  (unsigned int)ATON_INTCTRL_INTREG_GET(0U),
                  (unsigned int)ATON_BUSIF_ERR_GET(0U));

    const uint32_t epoch_begin =
        (s_epoch_total > kEpochRecordCapacity)
            ? s_epoch_total - kEpochRecordCapacity
            : 0U;
    for (uint32_t sequence = epoch_begin; sequence < s_epoch_total;
         ++sequence) {
        const volatile EpochRecord *record =
            &s_epoch_records[sequence % kEpochRecordCapacity];
        if (record->committed != sequence + 1U) {
            continue;
        }
        SEGDIAG_PRINT((const UB *)
                          "segdiag: epoch n=%u ev=%u ep=%d-%d flags=%x wait=%x blob=%x irq=%u last=%x ctrl=%x addr=%x ecirq=%x int=%x bus=%x\n",
                      (unsigned int)record->sequence,
                      (unsigned int)record->event, (int)record->epoch,
                      (int)record->last_epoch, (unsigned int)record->flags,
                      (unsigned int)record->wait_mask,
                      (unsigned int)record->blob_address,
                      (unsigned int)record->irq_count,
                      (unsigned int)record->last_irqs,
                      (unsigned int)record->epoch_control,
                      (unsigned int)record->epoch_address,
                      (unsigned int)record->epoch_irq,
                      (unsigned int)record->interrupt_status,
                      (unsigned int)record->bus_error);
    }

    const uint32_t irq_begin =
        (s_irq_total > kIrqRecordCapacity)
            ? s_irq_total - kIrqRecordCapacity
            : 0U;
    for (uint32_t sequence = irq_begin; sequence < s_irq_total;
         ++sequence) {
        const volatile IrqRecord *record =
            &s_irq_records[sequence % kIrqRecordCapacity];
        if (record->committed != sequence + 1U) {
            continue;
        }
        SEGDIAG_PRINT((const UB *)
                          "segdiag: irq n=%u edge=%u count=%u last=%x ctrl=%x addr=%x ecirq=%x int=%x bus=%x\n",
                      (unsigned int)record->sequence,
                      (unsigned int)record->edge,
                      (unsigned int)record->irq_count,
                      (unsigned int)record->last_irqs,
                      (unsigned int)record->epoch_control,
                      (unsigned int)record->epoch_address,
                      (unsigned int)record->epoch_irq,
                      (unsigned int)record->interrupt_status,
                      (unsigned int)record->bus_error);
    }

    const uint32_t poll_begin =
        (s_poll_total > kPollRecordCapacity)
            ? s_poll_total - kPollRecordCapacity
            : 0U;
    for (uint32_t sequence = poll_begin; sequence < s_poll_total;
         ++sequence) {
        const volatile PollRecord *record =
            &s_poll_records[sequence % kPollRecordCapacity];
        if (record->committed != sequence + 1U) {
            continue;
        }
        SEGDIAG_PRINT((const UB *)
                          "segdiag: poll n=%u tick=%u st=%x irq=%u last=%x ctrl=%x addr=%x ecirq=%x int=%x bus=%x\n",
                      (unsigned int)sequence, (unsigned int)record->tick,
                      (unsigned int)record->stai_status,
                      (unsigned int)record->irq_count,
                      (unsigned int)record->last_irqs,
                      (unsigned int)record->epoch_control,
                      (unsigned int)record->epoch_address,
                      (unsigned int)record->epoch_irq,
                      (unsigned int)record->interrupt_status,
                      (unsigned int)record->bus_error);
    }
}
