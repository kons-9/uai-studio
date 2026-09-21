#include "memory_manager.hpp"

#include <cstdint>

extern "C"
{
#include "main.h"
#include "mx66uw1g45g.h"
#include "stm32n6570_discovery_xspi.h"
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>

    extern XSPI_HandleTypeDef hxspi2;
    void npu_cache_enable(void);
}

/* The generated CubeMX adapter owns hxspi2; sample2 configures that handle
 * here before using the external-memory manager. */

namespace memory
{

    namespace
    {

        constexpr std::uintptr_t kInternalMediaBase = 0x34200000UL;
        constexpr std::size_t kInternalMediaSize = 0x001C0000UL;
        constexpr std::size_t kModelExternalPoolSize = 0x01000000UL;
        constexpr std::size_t kCacheLineSize = 32U;

        std::uintptr_t align_up(std::uintptr_t value, std::size_t alignment)
        {
            const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment - 1U);
            return (value + mask) & ~mask;
        }

        void configure_external_memory_access()
        {
            __HAL_RCC_RIFSC_CLK_ENABLE();

            /* AXISRAM3..6 are the four physical NPU RAM cuts.  On STM32N6 they are
             * protected by three RISAF ingress filters (NPU master 0, NPU master 1,
             * and the CPU view), and all three filters must describe the same access
             * policy.  The generated model uses the secure address view, so make the
             * complete NPU RAM aperture secure and readable/writable by every CID;
             * the NPU master itself is still configured secure/privileged below. */
            __HAL_RCC_RISAF_CLK_ENABLE();
            const auto configure_npu_ram_risaf = [](RISAF_TypeDef *risaf,
                                                    std::uint32_t end_address)
            {
                risaf->REG[0].CFGR = 0U;
                risaf->REG[0].STARTR = 0U;
                risaf->REG[0].ENDR = end_address;
                risaf->REG[0].CIDCFGR = 0x000F000FUL;
                risaf->REG[0].CFGR = 0x00FF0101UL;
            };
            configure_npu_ram_risaf(RISAF4, 0xFFFFFFFFUL);
            configure_npu_ram_risaf(RISAF5, 0xFFFFFFFFUL);
            configure_npu_ram_risaf(RISAF6, 0xFFFFFFFFUL);

            /* The application image and the generated epoch-controller blobs are
             * loaded at 0x34000000, which is the FLEXRAM aperture on STM32N6570.
             * RISAF7 is independent of the AXISRAM filters above; leaving it at its
             * reset configuration lets the CPU read the blob while the NPU gets an
             * ERR_START on its first fetch. */
            configure_npu_ram_risaf(RISAF7, 0x00063FFFUL);

            /* RISAF12 filters the XSPI2 memory aperture.  The generated epoch blobs
             * address weights throughout the pool beginning at 0x70380000.  Use the
             * complete XSPI2 offset space so every generated transfer is accepted. */
            RISAF12->REG[0].CFGR = 0U;
            RISAF12->REG[0].STARTR = 0x00000000UL;
            RISAF12->REG[0].ENDR = RISAF12_LIMIT_ADDRESS_SPACE_SIZE;
            RISAF12->REG[0].CIDCFGR = (RIF_CID_MASK << 16) | RIF_CID_MASK;
            RISAF12->REG[0].CFGR = 0x00FF0101UL;

            /* The epoch-controller command blobs are linked into AXISRAM1
             * (0x34000000).  The NPU must be able to fetch them through RISAF2 before
             * it can execute the first epoch. */
            RISAF2->REG[0].CFGR = 0U;
            RISAF2->REG[0].STARTR = 0U;
            RISAF2->REG[0].ENDR = 0x000FFFFFUL;
            RISAF2->REG[0].CIDCFGR = 0x000F000FUL;
            RISAF2->REG[0].CFGR = 0x00FF0101UL;

            /* The activation/input pool starts in AXISRAM2 (0x34100000). */
            RISAF3->REG[0].CFGR = 0U;
            RISAF3->REG[0].STARTR = 0U;
            RISAF3->REG[0].ENDR = 0x000FFFFFUL;
            RISAF3->REG[0].CIDCFGR = 0x000F000FUL;
            RISAF3->REG[0].CFGR = 0x00FF0101UL;

            RIMC_MasterConfig_t master = {};
            master.MasterCID = RIF_CID_1;
            master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;

            /* CubeMX assigns the NPU RIMC master to CID0 (NPU_CID_RIMU=0).  This is
             * separate from the secure CPU/media masters, which use CID1. */
            RIMC_MasterConfig_t npu_master = master;
            npu_master.MasterCID = RIF_CID_0;

            /* Make the external frame-buffer path self-contained. */
            HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &npu_master);
            HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &master);
            HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &master);
            HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &master);
            HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2, &master);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_XSPI1,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_XSPI2,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_XSPIM,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_NPU,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_DMA2D,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_CSI,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_DCMIPP,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_LTDC,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_LTDCL1,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_LTDCL2,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_CACHEAXIRAM,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_CACHECONFIG,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_NPURAM0,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_NPURAM1,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_NPURAM2,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_NPURAM3,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_AXISRAM1,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_AXISRAM2,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RCC_PERIPH_INDEX_FLEXRAM,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

            /* Keep a useful diagnostic if a future RIF change blocks an NPU access. */
            HAL_RIF_IAC_EnableIT(RIF_RISC_PERIPH_INDEX_XSPI2);
            HAL_RIF_IAC_EnableIT(RIF_RISC_PERIPH_INDEX_NPU);
            HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF2);
            HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF3);
            HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF4);
            HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF5);
            HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF7);
            HAL_RIF_IAC_EnableIT(RIF_AWARE_PERIPH_INDEX_RISAF12);
            HAL_NVIC_SetPriority(IAC_IRQn, 0U, 0U);
            HAL_NVIC_EnableIRQ(IAC_IRQn);
        }

        void enable_npu_ram()
        {
            /* Neural-ART accesses these blocks directly.  They must be powered and
             * clocked before LL_ATON touches any NPU register or generated pool. */
            __HAL_RCC_NPU_CLK_ENABLE();
            __HAL_RCC_NPU_FORCE_RESET();
            __HAL_RCC_NPU_RELEASE_RESET();

            /* The application image and command blobs occupy FLEXRAM at
             * 0x34000000.  Its memory clock is separate from AXISRAM1..6. */
            __HAL_RCC_FLEXRAM_MEM_CLK_ENABLE();

            /* The generated model's input and virtual activation pool start at
             * 0x34100000, which is AXISRAM2 on STM32N6570.  Camera/display setup
             * only enables AXISRAM3..6, so make AXISRAM1..2 explicit here as well.
             * The command blobs in FLEXRAM are fetched by the NPU, even though the
             * CPU can already execute from that region through the FSBL setup. */
            __HAL_RCC_AXISRAM1_MEM_CLK_ENABLE();
            __HAL_RCC_AXISRAM2_MEM_CLK_ENABLE();
            __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
            __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
            __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
            __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
            __HAL_RCC_RAMCFG_CLK_ENABLE();

            RAMCFG_HandleTypeDef ramcfg = {};
            ramcfg.Instance = RAMCFG_SRAM2_AXI;
            HAL_RAMCFG_EnableAXISRAM(&ramcfg);
            ramcfg.Instance = RAMCFG_SRAM3_AXI;
            HAL_RAMCFG_EnableAXISRAM(&ramcfg);
            ramcfg.Instance = RAMCFG_SRAM4_AXI;
            HAL_RAMCFG_EnableAXISRAM(&ramcfg);
            ramcfg.Instance = RAMCFG_SRAM5_AXI;
            HAL_RAMCFG_EnableAXISRAM(&ramcfg);
            ramcfg.Instance = RAMCFG_SRAM6_AXI;
            HAL_RAMCFG_EnableAXISRAM(&ramcfg);

            /* The generated Neural-ART pools use the interleaved AXI-SRAM view.
             * Without this SYSCFG setting the runtime can initialize successfully,
             * but the first epoch never reaches the idle state. */
            __HAL_RCC_SYSCFG_CLK_ENABLE();
            HAL_SYSCFG_EnableInterleavingCpuRam();

            /* Make illegal NPU/RIF accesses observable through the dedicated IAC
             * interrupt instead of leaving the runtime waiting indefinitely. */
            __HAL_RCC_IAC_CLK_ENABLE();
            __HAL_RCC_IAC_FORCE_RESET();
            __HAL_RCC_IAC_RELEASE_RESET();
        }

        extern "C" void npu_cache_enable_clocks_and_reset(void)
        {
            __HAL_RCC_CACHEAXI_CLK_ENABLE();
            __HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
            __HAL_RCC_CACHEAXI_FORCE_RESET();
            __HAL_RCC_CACHEAXI_RELEASE_RESET();
        }

        bool configure_external_clocks()
        {
            RCC_PeriphCLKInitTypeDef clocks = {};
            clocks.PeriphClockSelection = RCC_PERIPHCLK_XSPI1 | RCC_PERIPHCLK_XSPI2;
            clocks.Xspi1ClockSelection = RCC_XSPI1CLKSOURCE_HCLK;
            clocks.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_HCLK;
            return HAL_RCCEx_PeriphCLKConfig(&clocks) == HAL_OK;
        }

        void configure_xspi2_rif_access()
        {
            __HAL_RCC_RIFSC_CLK_ENABLE();
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_XSPI2,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
            HAL_RIF_RISC_SetSlaveSecureAttributes(
                RIF_RISC_PERIPH_INDEX_XSPIM,
                RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
        }

        bool configure_model_nor_peripheral()
        {
            /* The NOR I/O bank is 1.8 V.  The generated XSPI2 MSP configures the
             * pins, but the DK NOR BSP also explicitly selects this VDDIO range. */
            __HAL_RCC_PWR_CLK_ENABLE();
            HAL_PWREx_EnableVddIO3();
            HAL_PWREx_ConfigVddIORange(PWR_VDDIO3, PWR_VDDIO_RANGE_1V8);

            __HAL_RCC_XSPIM_CLK_ENABLE();
            __HAL_RCC_XSPI2_CLK_ENABLE();
            __HAL_RCC_XSPI2_FORCE_RESET();
            __HAL_RCC_XSPI2_RELEASE_RESET();

            hxspi2.Instance = XSPI2;
            /* FSBL initialized the same global handle and left its HAL state READY.
             * The peripheral itself was reset above, so force the handle through the
             * HAL reset path as well; otherwise HAL_XSPI_Init() silently skips all
             * register programming and later memory-mapped reads stall. */
            hxspi2.State = HAL_XSPI_STATE_RESET;
            hxspi2.Init.FifoThresholdByte = 1U;
            hxspi2.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
            hxspi2.Init.MemoryType = HAL_XSPI_MEMTYPE_MACRONIX;
            hxspi2.Init.MemorySize = HAL_XSPI_SIZE_1GB;
            hxspi2.Init.ChipSelectHighTimeCycle = 2U;
            hxspi2.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
            hxspi2.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
            hxspi2.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
            hxspi2.Init.ClockPrescaler = 3U;
            hxspi2.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
            hxspi2.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_ENABLE;
            hxspi2.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
            hxspi2.Init.MaxTran = 0U;
            hxspi2.Init.Refresh = 0U;
            hxspi2.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;

            if (HAL_XSPI_Init(&hxspi2) != HAL_OK)
            {
                return false;
            }

            XSPIM_CfgTypeDef crossbar = {};
            crossbar.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
            crossbar.IOPort = HAL_XSPIM_IOPORT_2;
            crossbar.Req2AckTime = 1U;
            if (HAL_XSPIM_Config(&hxspi2, &crossbar,
                                 HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
            {
                return false;
            }

            /* The DK BSP adds a pull-up to the NOR chip-select.  The CubeMX FSBL MSP
             * leaves this pin floating, which can prevent the first indirect command
             * from completing after the application takes over XSPI2. */
            GPIO_InitTypeDef nor_cs = {};
            nor_cs.Pin = OCTOSPI_NCS_Pin;
            nor_cs.Mode = GPIO_MODE_AF_PP;
            nor_cs.Pull = GPIO_PULLUP;
            nor_cs.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
            nor_cs.Alternate = GPIO_AF9_XSPIM_P2;
            HAL_GPIO_Init(OCTOSPI_NCS_GPIO_Port, &nor_cs);

            /* The external loader may leave the Macronix device in any of its
             * supported protocol states.  Return it to the power-on SPI state, then
             * configure OPI-DTR explicitly before issuing the memory-map command. */
            const int32_t reset_spi_enable = MX66UW1G45G_ResetEnable(
                &hxspi2, MX66UW1G45G_SPI_MODE, MX66UW1G45G_STR_TRANSFER);
            const int32_t reset_spi = MX66UW1G45G_ResetMemory(
                &hxspi2, MX66UW1G45G_SPI_MODE, MX66UW1G45G_STR_TRANSFER);
            const int32_t reset_opi_str_enable = MX66UW1G45G_ResetEnable(
                &hxspi2, MX66UW1G45G_OPI_MODE, MX66UW1G45G_STR_TRANSFER);
            const int32_t reset_opi_str = MX66UW1G45G_ResetMemory(
                &hxspi2, MX66UW1G45G_OPI_MODE, MX66UW1G45G_STR_TRANSFER);
            const int32_t reset_opi_dtr_enable = MX66UW1G45G_ResetEnable(
                &hxspi2, MX66UW1G45G_OPI_MODE, MX66UW1G45G_DTR_TRANSFER);
            const int32_t reset_opi_dtr = MX66UW1G45G_ResetMemory(
                &hxspi2, MX66UW1G45G_OPI_MODE, MX66UW1G45G_DTR_TRANSFER);
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: nor reset=%x,%x,%x,%x,%x,%x\n"),
                      static_cast<unsigned int>(reset_spi_enable),
                      static_cast<unsigned int>(reset_spi),
                      static_cast<unsigned int>(reset_opi_str_enable),
                      static_cast<unsigned int>(reset_opi_str),
                      static_cast<unsigned int>(reset_opi_dtr_enable),
                      static_cast<unsigned int>(reset_opi_dtr));
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: nor hw state=%x err=%x sr=%x cr=%x dcr1=%x dcr2=%x iom=%x\n"),
                      static_cast<unsigned int>(hxspi2.State),
                      static_cast<unsigned int>(hxspi2.ErrorCode),
                      static_cast<unsigned int>(XSPI2->SR),
                      static_cast<unsigned int>(XSPI2->CR),
                      static_cast<unsigned int>(XSPI2->DCR1),
                      static_cast<unsigned int>(XSPI2->DCR2),
                      static_cast<unsigned int>(XSPIM->CR));
            if (reset_spi_enable != MX66UW1G45G_OK ||
                reset_spi != MX66UW1G45G_OK ||
                reset_opi_str_enable != MX66UW1G45G_OK ||
                reset_opi_str != MX66UW1G45G_OK ||
                reset_opi_dtr_enable != MX66UW1G45G_OK ||
                reset_opi_dtr != MX66UW1G45G_OK)
            {
                return false;
            }
            HAL_Delay(1U);

            const int32_t write_enable_1 = MX66UW1G45G_WriteEnable(
                &hxspi2, MX66UW1G45G_SPI_MODE, MX66UW1G45G_STR_TRANSFER);
            const int32_t write_dc = MX66UW1G45G_WriteCfg2Register(
                &hxspi2, MX66UW1G45G_SPI_MODE, MX66UW1G45G_STR_TRANSFER,
                MX66UW1G45G_CR2_REG3_ADDR, MX66UW1G45G_CR2_DC_20_CYCLES);
            const int32_t write_enable_2 = MX66UW1G45G_WriteEnable(
                &hxspi2, MX66UW1G45G_SPI_MODE, MX66UW1G45G_STR_TRANSFER);
            const int32_t write_dopi = MX66UW1G45G_WriteCfg2Register(
                &hxspi2, MX66UW1G45G_SPI_MODE, MX66UW1G45G_STR_TRANSFER,
                MX66UW1G45G_CR2_REG1_ADDR, MX66UW1G45G_CR2_DOPI);
            tm_printf(reinterpret_cast<const UB *>("ai: nor mode=%x,%x,%x,%x\n"),
                      static_cast<unsigned int>(write_enable_1),
                      static_cast<unsigned int>(write_dc),
                      static_cast<unsigned int>(write_enable_2),
                      static_cast<unsigned int>(write_dopi));
            if (write_enable_1 != MX66UW1G45G_OK ||
                write_dc != MX66UW1G45G_OK ||
                write_enable_2 != MX66UW1G45G_OK ||
                write_dopi != MX66UW1G45G_OK)
            {
                return false;
            }
            HAL_Delay(40U);
            return MX66UW1G45G_AutoPollingMemReady(
                       &hxspi2, MX66UW1G45G_OPI_MODE,
                       MX66UW1G45G_DTR_TRANSFER) == MX66UW1G45G_OK;
        }

        void cache_operation(const Buffer &buffer, bool invalidate, bool clean)
        {
            if (!buffer)
            {
                return;
            }

            const std::uintptr_t start =
                buffer.address & ~static_cast<std::uintptr_t>(kCacheLineSize - 1U);
            const std::uintptr_t end =
                align_up(buffer.address + buffer.size, kCacheLineSize);
            const int32_t length = static_cast<int32_t>(end - start);
            auto *address = reinterpret_cast<uint32_t *>(start);

            if (clean)
            {
                SCB_CleanDCache_by_Addr(address, length);
            }
            if (invalidate)
            {
                SCB_InvalidateDCache_by_Addr(address, length);
            }
        }

    } // namespace

    bool Manager::Initialize(bool reserve_model_pools)
    {
        if (initialized_)
        {
            return true;
        }

        internal_ = {kInternalMediaBase, kInternalMediaSize, 0U};
        psram_ = {static_cast<std::uintptr_t>(XSPI1_BASE),
                  static_cast<std::size_t>(APS256XX_RAM_SIZE), 0U};
        if (reserve_model_pools)
        {
            /* The generated person network declares a 16 MiB absolute xSPI1
             * virtual pool at 0x90000000. Application buffers must start at
             * 0x91000000 or later. */
            psram_.next = kModelExternalPoolSize;
        }

        if (!configure_external_clocks())
        {
            return false;
        }

        /* Do this independently of the LCD driver.  The generated model places
         * its command/activation pools in AXISRAM3..6, and stai_runtime_init()
         * accesses the NPU before any display code is relevant. */
        enable_npu_ram();

        if (
            BSP_XSPI_RAM_Init(0) != BSP_ERROR_NONE ||
            BSP_XSPI_RAM_EnableMemoryMappedMode(0) != BSP_ERROR_NONE)
        {
            return false;
        }

        initialized_ = true;
        return true;
    }

    void Manager::KeepInferenceClocksOnSleep() const
    {
        /* LL_ATON_RT_ASYNC waits with WFE between NPU events.  Without these
         * low-power clock enables, the NPU or the active frame-buffer path can
         * stop while the CPU sleeps. */
        __HAL_RCC_XSPI1_CLK_SLEEP_ENABLE();
        __HAL_RCC_XSPI2_CLK_SLEEP_ENABLE();
        __HAL_RCC_NPU_CLK_SLEEP_ENABLE();
        __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE();
        __HAL_RCC_CACHEAXIRAM_MEM_CLK_SLEEP_ENABLE();
        __HAL_RCC_LTDC_CLK_SLEEP_ENABLE();
        __HAL_RCC_DMA2D_CLK_SLEEP_ENABLE();
        __HAL_RCC_DCMIPP_CLK_SLEEP_ENABLE();
        __HAL_RCC_CSI_CLK_SLEEP_ENABLE();
        __HAL_RCC_FLEXRAM_MEM_CLK_SLEEP_ENABLE();
        __HAL_RCC_AXISRAM1_MEM_CLK_SLEEP_ENABLE();
        __HAL_RCC_AXISRAM2_MEM_CLK_SLEEP_ENABLE();
        __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();
        __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();
        __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
        __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();
    }

    bool Manager::InitializeModelStorage()
    {
        if (!initialized_)
        {
            return false;
        }
        if (model_storage_initialized_)
        {
            return true;
        }

        /* XSPI2 is a RIF-protected slave.  Configure only the XSPI path before
         * the first NOR command; NPU/RISAF/IAC setup is completed after mapping. */
        configure_xspi2_rif_access();

        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: nor peripheral begin\n")));
        if (!configure_model_nor_peripheral())
        {
            tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                "ai: nor peripheral failed\n")));
            return false;
        }
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: nor peripheral ready\n")));
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: nor direct map begin\n")));
        /* The Macronix OPI-DTR read command is specified for the bypassed
         * 200-MHz XSPI clock, as used by the DK BSP. */
        (void)HAL_XSPI_SetClockPrescaler(&hxspi2, 0U);
        const int32_t map_status = MX66UW1G45G_EnableDTRMemoryMappedMode(
            &hxspi2, MX66UW1G45G_OPI_MODE);
        tm_printf(reinterpret_cast<const UB *>("ai: nor direct map=%x\n"),
                  static_cast<unsigned int>(map_status));
        if (map_status != MX66UW1G45G_OK)
        {
            tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                "ai: nor map failed\n")));
            return false;
        }
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: nor map ready\n")));

        configure_external_memory_access();

        const volatile std::uint32_t *nor_data =
            reinterpret_cast<const volatile std::uint32_t *>(0x70380000UL);
        tm_printf(reinterpret_cast<const UB *>("ai: nor data=%x,%x\n"),
                  static_cast<unsigned int>(nor_data[0]),
                  static_cast<unsigned int>(nor_data[1]));

        /* CACHEAXI accesses the same RIF-controlled external-memory path as the
         * NPU.  Enable it only after all RIF and memory-mapped XSPI settings are
         * in place. */
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: npu cache begin\n")));
        npu_cache_enable();
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "ai: npu cache ready\n")));

        model_storage_initialized_ = true;
        return true;
    }

    Buffer Manager::Allocate(Region region, std::size_t size,
                             std::size_t alignment)
    {
        if (!initialized_ || size == 0U || alignment == 0U ||
            (alignment & (alignment - 1U)) != 0U)
        {
            return {};
        }

        RegionState &state =
            (region == Region::kExternalPsram) ? psram_ : internal_;
        const std::uintptr_t current = state.base + state.next;
        const std::uintptr_t address = align_up(current, alignment);
        const std::size_t offset = static_cast<std::size_t>(address - state.base);

        if (offset > state.size || size > state.size - offset)
        {
            return {};
        }

        state.next = offset + size;
        return {address, size, region};
    }

    void Manager::PrepareForDmaWrite(const Buffer &buffer) const
    {
        /* Remove dirty CPU lines before a peripheral starts overwriting memory. */
        cache_operation(buffer, true, true);
    }

    void Manager::PrepareForCpuRead(const Buffer &buffer) const
    {
        /* The camera is the producer, so discard CPU lines after the frame event. */
        cache_operation(buffer, true, false);
    }

    void Manager::PrepareForDisplayRead(const Buffer &buffer) const
    {
        /* LTDC/DMA2D reads memory independently from the CPU. */
        cache_operation(buffer, false, true);
    }

} // namespace memory
