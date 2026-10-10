#include "tests/board.hpp"

extern "C" {
#include "driver/c_bsp/xspi_bsp.h"
#include "mx66uw1g45g.h"
#include "stm32n6570_discovery_xspi.h"
extern XSPI_NOR_Ctx_t XSPI_Nor_Ctx[];

int32_t __real_MX66UW1G45G_ReadStatusRegister(
    XSPI_HandleTypeDef *,
    MX66UW1G45G_Interface_t,
    MX66UW1G45G_Transfer_t,
    uint8_t *
);
int32_t __real_MX66UW1G45G_ResetEnable(
    XSPI_HandleTypeDef *,
    MX66UW1G45G_Interface_t,
    MX66UW1G45G_Transfer_t
);
int32_t __real_MX66UW1G45G_ResetMemory(
    XSPI_HandleTypeDef *,
    MX66UW1G45G_Interface_t,
    MX66UW1G45G_Transfer_t
);
HAL_StatusTypeDef __real_HAL_XSPI_Command(
    XSPI_HandleTypeDef *,
    const XSPI_RegularCmdTypeDef *,
    uint32_t
);
HAL_StatusTypeDef __real_HAL_XSPI_Receive(
    XSPI_HandleTypeDef *,
    uint8_t *,
    uint32_t
);
}

#include <cstdio>

namespace experiment::hwtest::tests::nor_driver {

namespace {

const Context *trace_context;

const char *ModeName(MX66UW1G45G_Interface_t mode)
{
    return mode == MX66UW1G45G_SPI_MODE ? "spi" : "opi";
}

const char *RateName(MX66UW1G45G_Transfer_t rate)
{
    return rate == MX66UW1G45G_STR_TRANSFER ? "str" : "dtr";
}

const char *DiagnosticName(uint32_t code)
{
    switch (code) {
    case UAI_NOR_DIAG_NONE:
        return "none";
    case UAI_NOR_DIAG_TIMEOUT_BUSY_NO_EVENT:
        return "timeout-busy-no-event";
    case UAI_NOR_DIAG_TIMEOUT_BUSY_WITH_EVENT:
        return "timeout-busy-with-event";
    case UAI_NOR_DIAG_TIMEOUT_NOT_BUSY:
        return "timeout-not-busy";
    case UAI_NOR_DIAG_HAL_ERROR:
        return "hal-error";
    case UAI_NOR_DIAG_COMPONENT_FAILURE_NO_HAL_ERROR:
        return "component-failure-no-hal-error";
    default:
        return "unknown";
    }
}

const char *DiagnosticStageName(uint32_t stage)
{
    switch (stage) {
    case UAI_NOR_DIAG_STAGE_XSPI_INIT:
        return "xspi-init";
    case UAI_NOR_DIAG_STAGE_RESET_ENABLE_OPI_DTR:
        return "opi-dtr-reset-enable";
    case UAI_NOR_DIAG_STAGE_RESET_OPI_DTR:
        return "opi-dtr-reset";
    case UAI_NOR_DIAG_STAGE_MEM_READY:
        return "mem-ready";
    case UAI_NOR_DIAG_STAGE_FLASH_CONFIG:
        return "flash-config";
    case UAI_NOR_DIAG_STAGE_CLOCK_CONFIG:
        return "clock-config";
    case UAI_NOR_DIAG_STAGE_XSPIM_CONFIG:
        return "xspim-config";
    case UAI_NOR_DIAG_STAGE_STR_READ:
        return "str-read";
    case UAI_NOR_DIAG_STAGE_DTR_READ:
        return "dtr-read";
    default:
        return "other";
    }
}

void TraceDriverDiagnostic(const Context &context)
{
    if (uai_nor_diag_code == UAI_NOR_DIAG_NONE) {
        return;
    }

    char line[240];
    std::snprintf(
        line,
        sizeof(line),
        "TRACE nor diag=%s stage=%s/%lu result=%ld err=%08lx state=%lu sr=%08lx cr=%08lx ccr=%08lx dlr=%08lx ir=%08lx "
        "ar=%08lx xspim=%08lx",
        DiagnosticName(uai_nor_diag_code),
        DiagnosticStageName(uai_nor_diag_stage),
        static_cast<unsigned long>(uai_nor_diag_stage),
        static_cast<long>(uai_nor_diag_component_result),
        static_cast<unsigned long>(uai_nor_diag_hal_error),
        static_cast<unsigned long>(uai_nor_diag_hal_state),
        static_cast<unsigned long>(uai_nor_diag_sr),
        static_cast<unsigned long>(uai_nor_diag_cr),
        static_cast<unsigned long>(uai_nor_diag_ccr),
        static_cast<unsigned long>(uai_nor_diag_dlr),
        static_cast<unsigned long>(uai_nor_diag_ir),
        static_cast<unsigned long>(uai_nor_diag_ar),
        static_cast<unsigned long>(uai_nor_diag_xspim_cr)
    );
    context.Trace(line);
}

void TraceComponentCall(
    const char *operation,
    const char *phase,
    XSPI_HandleTypeDef *handle,
    MX66UW1G45G_Interface_t mode,
    MX66UW1G45G_Transfer_t rate,
    bool has_status,
    int32_t status,
    const uint8_t *value = nullptr
)
{
    if (!trace_context) {
        return;
    }

    const auto state = handle ? static_cast<unsigned long>(handle->State) : 0UL;
    const auto error = handle ? static_cast<unsigned long>(handle->ErrorCode) : 0UL;
    const auto sr = handle && handle->Instance ? static_cast<unsigned long>(handle->Instance->SR) : 0UL;
    const auto cr = handle && handle->Instance ? static_cast<unsigned long>(handle->Instance->CR) : 0UL;
    const auto ccr = handle && handle->Instance ? static_cast<unsigned long>(handle->Instance->CCR) : 0UL;
    char line[192];
    if (has_status) {
        std::snprintf(
            line,
            sizeof(line),
            "TRACE nor cmd=%s phase=%s mode=%s rate=%s rc=%ld state=%lu err=%08lx sr=%08lx cr=%08lx ccr=%08lx "
            "val=%02x%02x",
            operation,
            phase,
            ModeName(mode),
            RateName(rate),
            static_cast<long>(status),
            state,
            error,
            sr,
            cr,
            ccr,
            value ? static_cast<unsigned>(value[0]) : 0U,
            value ? static_cast<unsigned>(value[1]) : 0U
        );
    } else {
        std::snprintf(
            line,
            sizeof(line),
            "TRACE nor cmd=%s phase=%s mode=%s rate=%s state=%lu err=%08lx sr=%08lx cr=%08lx ccr=%08lx",
            operation,
            phase,
            ModeName(mode),
            RateName(rate),
            state,
            error,
            sr,
            cr,
            ccr
        );
    }
    trace_context->Trace(line);
}

void TraceHalCall(
    const char *operation,
    const char *phase,
    XSPI_HandleTypeDef *handle,
    HAL_StatusTypeDef status,
    uint32_t timeout,
    const XSPI_RegularCmdTypeDef *command = nullptr,
    const uint8_t *data = nullptr
)
{
    if (!trace_context) {
        return;
    }

    const auto state = handle ? static_cast<unsigned long>(handle->State) : 0UL;
    const auto error = handle ? static_cast<unsigned long>(handle->ErrorCode) : 0UL;
    const auto sr = handle && handle->Instance ? static_cast<unsigned long>(handle->Instance->SR) : 0UL;
    const auto cr = handle && handle->Instance ? static_cast<unsigned long>(handle->Instance->CR) : 0UL;
    const auto ccr = handle && handle->Instance ? static_cast<unsigned long>(handle->Instance->CCR) : 0UL;
    const auto dlr = handle && handle->Instance ? static_cast<unsigned long>(handle->Instance->DLR) : 0UL;
    char line[256];
    if (command) {
        std::snprintf(
            line,
            sizeof(line),
            "TRACE nor hal=%s phase=%s rc=%d timeout=%lu instr=%08lx im=%lu iw=%lu idtr=%lu am=%lu dm=%lu len=%lu "
            "ddtr=%lu dummy=%lu dqs=%lu state=%lu err=%08lx sr=%08lx cr=%08lx ccr=%08lx dlr=%08lx",
            operation,
            phase,
            static_cast<int>(status),
            static_cast<unsigned long>(timeout),
            static_cast<unsigned long>(command->Instruction),
            static_cast<unsigned long>(command->InstructionMode),
            static_cast<unsigned long>(command->InstructionWidth),
            static_cast<unsigned long>(command->InstructionDTRMode),
            static_cast<unsigned long>(command->AddressMode),
            static_cast<unsigned long>(command->DataMode),
            static_cast<unsigned long>(command->DataLength),
            static_cast<unsigned long>(command->DataDTRMode),
            static_cast<unsigned long>(command->DummyCycles),
            static_cast<unsigned long>(command->DQSMode),
            state,
            error,
            sr,
            cr,
            ccr,
            dlr
        );
    } else {
        std::snprintf(
            line,
            sizeof(line),
            "TRACE nor hal=%s phase=%s rc=%d timeout=%lu state=%lu err=%08lx sr=%08lx cr=%08lx ccr=%08lx dlr=%08lx "
            "data=%02x%02x",
            operation,
            phase,
            static_cast<int>(status),
            static_cast<unsigned long>(timeout),
            state,
            error,
            sr,
            cr,
            ccr,
            dlr,
            data ? static_cast<unsigned>(data[0]) : 0U,
            data && handle && handle->Instance && handle->Instance->DLR >= 1U ? static_cast<unsigned>(data[1]) : 0U
        );
    }
    trace_context->Trace(line);
}

}

extern "C" HAL_StatusTypeDef __wrap_HAL_XSPI_Command(
    XSPI_HandleTypeDef *handle,
    const XSPI_RegularCmdTypeDef *command,
    uint32_t timeout
)
{
    TraceHalCall("command", "begin", handle, static_cast<HAL_StatusTypeDef>(-1), timeout, command);
    const auto status = __real_HAL_XSPI_Command(handle, command, timeout);
    TraceHalCall("command", "end", handle, status, timeout, command);
    return status;
}

extern "C" HAL_StatusTypeDef __wrap_HAL_XSPI_Receive(
    XSPI_HandleTypeDef *handle,
    uint8_t *data,
    uint32_t timeout
)
{
    TraceHalCall("receive", "begin", handle, static_cast<HAL_StatusTypeDef>(-1), timeout);
    const auto status = __real_HAL_XSPI_Receive(handle, data, timeout);
    TraceHalCall("receive", "end", handle, status, timeout, nullptr, data);
    return status;
}

extern "C" int32_t __wrap_MX66UW1G45G_ReadStatusRegister(
    XSPI_HandleTypeDef *handle,
    MX66UW1G45G_Interface_t mode,
    MX66UW1G45G_Transfer_t rate,
    uint8_t *value
)
{
    TraceComponentCall("read-status", "begin", handle, mode, rate, false, 0);
    const auto status = __real_MX66UW1G45G_ReadStatusRegister(handle, mode, rate, value);
    TraceComponentCall("read-status", "end", handle, mode, rate, true, status, value);
    return status;
}

extern "C" int32_t __wrap_MX66UW1G45G_ResetEnable(
    XSPI_HandleTypeDef *handle,
    MX66UW1G45G_Interface_t mode,
    MX66UW1G45G_Transfer_t rate
)
{
    TraceComponentCall("reset-enable", "begin", handle, mode, rate, false, 0);
    const auto status = __real_MX66UW1G45G_ResetEnable(handle, mode, rate);
    TraceComponentCall("reset-enable", "end", handle, mode, rate, true, status);
    return status;
}

extern "C" int32_t __wrap_MX66UW1G45G_ResetMemory(
    XSPI_HandleTypeDef *handle,
    MX66UW1G45G_Interface_t mode,
    MX66UW1G45G_Transfer_t rate
)
{
    TraceComponentCall("reset-memory", "begin", handle, mode, rate, false, 0);
    const auto status = __real_MX66UW1G45G_ResetMemory(handle, mode, rate);
    TraceComponentCall("reset-memory", "end", handle, mode, rate, true, status);
    return status;
}

Result Run(const Context &context)
{
    trace_context = &context;
    context.Trace("TRACE nor stage=secure-peripheral begin");
    SecurePeripheral(RIF_RISC_PERIPH_INDEX_XSPI2);
    context.Trace("TRACE nor stage=secure-peripheral done");

    BSP_XSPI_NOR_Init_t configuration{};
    configuration.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
    configuration.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;
    static char detail[192];

    context.Trace("TRACE nor stage=bsp-init begin mode=opi rate=dtr");
    const auto init_begin = context.clock();
    const auto init_status = BSP_XSPI_NOR_Init(0, &configuration);
    const auto init_ms = context.clock() - init_begin;
    char line[224];
    std::snprintf(
        line,
        sizeof(line),
        "TRACE nor stage=bsp-init status=%ld ms=%lu ctx=%u mode=%u rate=%u hal_err=%08lx hal_state=%lu sr=%08lx "
        "cr=%08lx ccr=%08lx",
        static_cast<long>(init_status),
        static_cast<unsigned long>(init_ms),
        static_cast<unsigned>(XSPI_Nor_Ctx[0].IsInitialized),
        static_cast<unsigned>(XSPI_Nor_Ctx[0].InterfaceMode),
        static_cast<unsigned>(XSPI_Nor_Ctx[0].TransferRate),
        static_cast<unsigned long>(hxspi_nor[0].ErrorCode),
        static_cast<unsigned long>(hxspi_nor[0].State),
        static_cast<unsigned long>(XSPI2->SR),
        static_cast<unsigned long>(XSPI2->CR),
        static_cast<unsigned long>(XSPI2->CCR)
    );
    context.Trace(line);
    TraceDriverDiagnostic(context);
    trace_context = nullptr;

    if (init_status != BSP_ERROR_NONE) {
        const auto xspi_error = hxspi_nor[0].ErrorCode;
        const char *xspi_error_name = (xspi_error & HAL_XSPI_ERROR_TIMEOUT) != 0U ? "timeout" : "other";
        const char *context_name = XSPI_Nor_Ctx[0].IsInitialized == XSPI_ACCESS_NONE
            ? "none"
            : (XSPI_Nor_Ctx[0].IsInitialized == XSPI_ACCESS_INDIRECT ? "indirect" : "mapped");
        const char *mode_name = XSPI_Nor_Ctx[0].InterfaceMode == BSP_XSPI_NOR_SPI_MODE ? "spi" : "opi";
        const char *rate_name = XSPI_Nor_Ctx[0].TransferRate == BSP_XSPI_NOR_STR_TRANSFER ? "str" : "dtr";
        std::snprintf(
            detail,
            sizeof(detail),
            "init=%ld ms=%lu xspi=%s ctx=%s mode=%s rate=%s diag=%s/%lu err=%08lx state=%lu sr=%08lx ccr=%08lx",
            static_cast<long>(init_status),
            static_cast<unsigned long>(init_ms),
            xspi_error_name,
            context_name,
            mode_name,
            rate_name,
            DiagnosticName(uai_nor_diag_code),
            static_cast<unsigned long>(uai_nor_diag_stage),
            static_cast<unsigned long>(uai_nor_diag_hal_error),
            static_cast<unsigned long>(hxspi_nor[0].State),
            static_cast<unsigned long>(XSPI2->SR),
            static_cast<unsigned long>(uai_nor_diag_ccr)
        );
        return {Outcome::kFail, detail};
    }

    const auto initialized_context = XSPI_Nor_Ctx[0].IsInitialized;
    const auto initialized_mode = XSPI_Nor_Ctx[0].InterfaceMode;
    const auto initialized_rate = XSPI_Nor_Ctx[0].TransferRate;
    const auto initialized_cr = XSPI2->CR;
    const auto initialized_sr = XSPI2->SR;
    if (initialized_context == XSPI_ACCESS_NONE || initialized_mode != BSP_XSPI_NOR_OPI_MODE
        || initialized_rate != BSP_XSPI_NOR_DTR_TRANSFER || (initialized_cr & XSPI_CR_EN) == 0U
        || (initialized_sr & XSPI_SR_BUSY) != 0U) {
        std::snprintf(
            detail,
            sizeof(detail),
            "init-reg ctx=%u mode=%u rate=%u cr=%08lx sr=%08lx",
            static_cast<unsigned>(initialized_context),
            static_cast<unsigned>(initialized_mode),
            static_cast<unsigned>(initialized_rate),
            static_cast<unsigned long>(initialized_cr),
            static_cast<unsigned long>(initialized_sr)
        );
        return {Outcome::kFail, detail};
    }

    std::uint8_t first[256]{}, second[256]{};
    context.Trace("TRACE nor stage=read1 begin addr=00000000 size=256");
    const auto read1_begin = context.clock();
    const auto read1_status = BSP_XSPI_NOR_Read(0, first, 0, sizeof(first));
    const auto read1_ms = context.clock() - read1_begin;
    std::snprintf(
        line,
        sizeof(line),
        "TRACE nor stage=read1 status=%ld ms=%lu err=%08lx sr=%08lx",
        static_cast<long>(read1_status),
        static_cast<unsigned long>(read1_ms),
        static_cast<unsigned long>(hxspi_nor[0].ErrorCode),
        static_cast<unsigned long>(XSPI2->SR)
    );
    context.Trace(line);
    if (read1_status != BSP_ERROR_NONE) {
        TraceDriverDiagnostic(context);
        std::snprintf(
            detail,
            sizeof(detail),
            "read1=%ld diag=%s/%lu err=%08lx sr=%08lx ccr=%08lx",
            static_cast<long>(read1_status),
            DiagnosticName(uai_nor_diag_code),
            static_cast<unsigned long>(uai_nor_diag_stage),
            static_cast<unsigned long>(uai_nor_diag_hal_error),
            static_cast<unsigned long>(uai_nor_diag_sr),
            static_cast<unsigned long>(uai_nor_diag_ccr)
        );
        return {Outcome::kFail, detail};
    }

    context.Trace("TRACE nor stage=read2 begin addr=00000000 size=256");
    const auto read2_begin = context.clock();
    const auto read2_status = BSP_XSPI_NOR_Read(0, second, 0, sizeof(second));
    const auto read2_ms = context.clock() - read2_begin;
    std::snprintf(
        line,
        sizeof(line),
        "TRACE nor stage=read2 status=%ld ms=%lu err=%08lx sr=%08lx",
        static_cast<long>(read2_status),
        static_cast<unsigned long>(read2_ms),
        static_cast<unsigned long>(hxspi_nor[0].ErrorCode),
        static_cast<unsigned long>(XSPI2->SR)
    );
    context.Trace(line);
    if (read2_status != BSP_ERROR_NONE) {
        TraceDriverDiagnostic(context);
        std::snprintf(
            detail,
            sizeof(detail),
            "read2=%ld diag=%s/%lu err=%08lx sr=%08lx ccr=%08lx",
            static_cast<long>(read2_status),
            DiagnosticName(uai_nor_diag_code),
            static_cast<unsigned long>(uai_nor_diag_stage),
            static_cast<unsigned long>(uai_nor_diag_hal_error),
            static_cast<unsigned long>(uai_nor_diag_sr),
            static_cast<unsigned long>(uai_nor_diag_ccr)
        );
        return {Outcome::kFail, detail};
    }

    for (std::size_t offset = 0; offset < sizeof(first); ++offset) {
        if (first[offset] != second[offset]) {
            std::snprintf(
                detail,
                sizeof(detail),
                "unstable offset=%lu first=%02x second=%02x init_ms=%lu",
                static_cast<unsigned long>(offset),
                static_cast<unsigned>(first[offset]),
                static_cast<unsigned>(second[offset]),
                static_cast<unsigned long>(init_ms)
            );
            return {Outcome::kFail, detail};
        }
    }

    std::snprintf(
        detail,
        sizeof(detail),
        "init_ms=%lu read1_ms=%lu read2_ms=%lu compare=stable",
        static_cast<unsigned long>(init_ms),
        static_cast<unsigned long>(read1_ms),
        static_cast<unsigned long>(read2_ms)
    );
    return {Outcome::kPass, detail};
}

}
