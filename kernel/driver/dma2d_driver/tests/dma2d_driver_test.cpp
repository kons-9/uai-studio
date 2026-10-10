#include "driver/dma2d_driver/dma2d_driver.hpp"
#include "middleware/image_processing/verification.hpp"
#include "stm32n6xx_hal.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

namespace dma = uai::ai::dma2d;
namespace graphics = uai::ai::image_processing;
using Code = uai::ai::common::ErrorCode;

struct HalState {
    HAL_StatusTypeDef initialize = HAL_OK;
    HAL_StatusTypeDef configure = HAL_OK;
    HAL_StatusTypeDef start = HAL_OK;
    HAL_StatusTypeDef poll = HAL_OK;
    HAL_StatusTypeDef abort = HAL_OK;
    ER lock = E_OK;
    DMA2D_HandleTypeDef handle{};
    std::uint32_t source = 0;
    std::uint32_t background = 0;
    std::uint32_t destination = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t timeout = 0;
    TMO lock_timeout = 0;
    std::vector<std::string> events;
} hal;

graphics::Request CopyRequest()
{
    return {
        graphics::Operation::kBlit,
        {reinterpret_cast<std::uint8_t *>(0x34000000U), 64, 8, 2, 24, graphics::Format::kRgb888},
        {},
        {reinterpret_cast<std::uint8_t *>(0x34100000U), 64, 8, 2, 24, graphics::Format::kRgb888}
    };
}

class Dma2dTest : public testing::Test {
protected:
    void SetUp() override
    {
        hal = {};
        const auto status = dma::Dma2dManagement::Instance().Initialize();
        ASSERT_TRUE(status.Ok() || status.Code() == Code::kAlreadyInitialized);
        hal = {};
    }
};

TEST_F(
    Dma2dTest,
    RejectsInvalidBuffersAndTimeoutsBeforeHalOrCacheAccess
)
{
    auto &management = dma::Dma2dManagement::Instance();
    auto request = CopyRequest();
    EXPECT_EQ(management.Transfer(request, 0).Code(), Code::kInvalidArgument);
    EXPECT_EQ(management.Transfer(request, 1001).Code(), Code::kInvalidArgument);
    request.destination.bytes = 32;
    EXPECT_EQ(management.Transfer(request).Code(), Code::kInvalidArgument);
    request = CopyRequest();
    request.destination = request.source;
    EXPECT_EQ(management.Transfer(request).Code(), Code::kInvalidArgument);
    request = CopyRequest();
    ++request.destination.data;
    EXPECT_EQ(management.Transfer(request).Code(), Code::kInvalidArgument);
    request = CopyRequest();
    request.operation = graphics::Operation::kResize;
    EXPECT_EQ(management.Transfer(request).Code(), Code::kInvalidArgument);
    EXPECT_TRUE(hal.events.empty());
}

TEST_F(
    Dma2dTest,
    CopyConfiguresRgbOrderAndMaintainsCacheInOrder
)
{
    ASSERT_TRUE(dma::Dma2dManagement::Instance().Transfer(CopyRequest(), 75).Ok());
    EXPECT_EQ(hal.handle.Init.Mode, DMA2D_M2M_PFC);
    EXPECT_EQ(hal.handle.Init.ColorMode, DMA2D_OUTPUT_RGB888);
    EXPECT_EQ(hal.handle.Init.RedBlueSwap, DMA2D_RB_SWAP);
    EXPECT_EQ(hal.handle.LayerCfg[1].RedBlueSwap, DMA2D_RB_SWAP);
    EXPECT_EQ(hal.source, 0x34000000U);
    EXPECT_EQ(hal.destination, 0x34100000U);
    EXPECT_EQ(hal.timeout, 75U);
    EXPECT_EQ(hal.lock_timeout, 75);
    EXPECT_EQ(
        hal.events,
        (std::vector<std::string>{
            "init", "layer1", "clean", "prepare", "barrier", "start", "poll", "barrier", "inspect", "barrier", "unlock"
        })
    );
}

TEST_F(
    Dma2dTest,
    FillSwapsRgb888ColorWithoutReadingAnInput
)
{
    auto request = CopyRequest();
    request.operation = graphics::Operation::kFill;
    request.source = {};
    request.color = 0x12ab34;
    ASSERT_TRUE(dma::Dma2dManagement::Instance().Run(request, 100));
    EXPECT_EQ(hal.handle.Init.Mode, DMA2D_R2M);
    EXPECT_EQ(hal.handle.Init.RedBlueSwap, DMA2D_RB_REGULAR);
    EXPECT_EQ(hal.source, 0x34ab12U);
    EXPECT_EQ(
        hal.events,
        (std::vector<std::string>{
            "init", "prepare", "barrier", "start", "poll", "barrier", "inspect", "barrier", "unlock"
        })
    );
}

TEST_F(
    Dma2dTest,
    TimeoutAbortsBeforeCacheInspectionAndAllowsReuse
)
{
    hal.poll = HAL_TIMEOUT;
    EXPECT_EQ(dma::Dma2dManagement::Instance().Transfer(CopyRequest()).Code(), Code::kTimeout);
    EXPECT_EQ(
        hal.events,
        (std::vector<std::string>{
            "init",
            "layer1",
            "clean",
            "prepare",
            "barrier",
            "start",
            "poll",
            "abort",
            "barrier",
            "inspect",
            "barrier",
            "unlock"
        })
    );
    hal = {};
    EXPECT_TRUE(dma::Dma2dManagement::Instance().Transfer(CopyRequest()).Ok());
}

TEST_F(
    Dma2dTest,
    AbortFailureResetsHardwareBeforeReturningBuffers
)
{
    hal.poll = HAL_TIMEOUT;
    hal.abort = HAL_ERROR;
    EXPECT_EQ(dma::Dma2dManagement::Instance().Transfer(CopyRequest()).Code(), Code::kTimeout);
    EXPECT_EQ(
        hal.events,
        (std::vector<std::string>{
            "init",
            "layer1",
            "clean",
            "prepare",
            "barrier",
            "start",
            "poll",
            "abort",
            "reset",
            "barrier",
            "release-reset",
            "barrier",
            "barrier",
            "inspect",
            "barrier",
            "unlock"
        })
    );
    hal = {};
    EXPECT_TRUE(dma::Dma2dManagement::Instance().Transfer(CopyRequest()).Ok());
}

TEST_F(
    Dma2dTest,
    CoversAll25VerificationCaseConfigurationsAndRejections
)
{
    for (const auto &test : graphics::kVerificationCases) {
        SCOPED_TRACE(test.name);
        hal = {};
        const std::uint32_t width = test.padded ? 7 : 8;
        graphics::Request request{
            test.operation,
            {reinterpret_cast<std::uint8_t *>(0x34000000U),
             64,
             width,
             2,
             8 * graphics::PixelBytes(test.source),
             test.source},
            {reinterpret_cast<std::uint8_t *>(0x34200000U),
             64,
             width,
             2,
             8 * graphics::PixelBytes(test.source),
             test.source},
            {reinterpret_cast<std::uint8_t *>(0x34100000U),
             64,
             width,
             2,
             8 * graphics::PixelBytes(test.destination),
             test.destination},
            test.color,
            test.alpha
        };
        switch (test.rejection) {
        case graphics::Rejection::kOverlap:
            request.destination = request.source;
            break;
        case graphics::Rejection::kBackgroundOverlap:
            request.background = request.destination;
            break;
        case graphics::Rejection::kShortBuffer:
            request.destination.bytes = 32;
            break;
        case graphics::Rejection::kStride:
            request.destination.stride = width * graphics::PixelBytes(test.destination) - 1;
            break;
        case graphics::Rejection::kUnaligned:
            ++request.destination.data;
            break;
        case graphics::Rejection::kResize:
            request.destination.width = 4;
            request.destination.height = 1;
            break;
        case graphics::Rejection::kNone:
            break;
        }
        const auto status = dma::Dma2dManagement::Instance().Transfer(request);
        if (test.rejection != graphics::Rejection::kNone) {
            EXPECT_EQ(status.Code(), Code::kInvalidArgument);
            EXPECT_TRUE(hal.events.empty());
            continue;
        }
        ASSERT_TRUE(status.Ok());
        EXPECT_EQ(hal.width, width);
        EXPECT_EQ(hal.height, 2U);
        EXPECT_EQ(hal.handle.Init.OutputOffset, test.padded ? 1U : 0U);
        EXPECT_EQ(
            hal.handle.Init.ColorMode,
            test.destination == graphics::Format::kRgb565 ? DMA2D_OUTPUT_RGB565 : DMA2D_OUTPUT_RGB888
        );
        if (test.operation != graphics::Operation::kFill) {
            EXPECT_EQ(hal.handle.LayerCfg[1].InputOffset, test.padded ? 1U : 0U);
            EXPECT_EQ(hal.handle.LayerCfg[1].InputAlpha, test.alpha);
            EXPECT_EQ(
                hal.handle.LayerCfg[1].InputColorMode,
                test.source == graphics::Format::kRgb565 ? DMA2D_INPUT_RGB565 : DMA2D_INPUT_RGB888
            );
        }
        if (test.operation == graphics::Operation::kBlend) {
            EXPECT_EQ(hal.handle.Init.Mode, DMA2D_M2M_BLEND);
            EXPECT_EQ(hal.handle.LayerCfg[0].InputAlpha, 255U);
            EXPECT_EQ(hal.background, 0x34200000U);
            EXPECT_EQ(std::count(hal.events.begin(), hal.events.end(), "clean"), 2);
        }
    }
}

TEST_F(
    Dma2dTest,
    RejectsCacheAndHardwareLimits
)
{
    auto request = CopyRequest();
    request.source.data++;
    EXPECT_FALSE(dma::Dma2dDriver::ValidateRequest(request, 100));
    request = CopyRequest();
    request.destination.bytes = 63;
    EXPECT_FALSE(dma::Dma2dDriver::ValidateRequest(request, 100));
    request = CopyRequest();
    request.destination.data = reinterpret_cast<std::uint8_t *>(0xffffffe0U);
    EXPECT_FALSE(dma::Dma2dDriver::ValidateRequest(request, 100));
    request = CopyRequest();
    request.operation = graphics::Operation::kFill;
    request.destination = {
        reinterpret_cast<std::uint8_t *>(0x34100000U), 32768, 16384, 1, 32768, graphics::Format::kRgb565
    };
    EXPECT_FALSE(dma::Dma2dDriver::ValidateRequest(request, 100));
    request.destination = {
        reinterpret_cast<std::uint8_t *>(0x34100000U), 32800, 8, 1, 32784, graphics::Format::kRgb565
    };
    EXPECT_FALSE(dma::Dma2dDriver::ValidateRequest(request, 100));
    request.destination = {
        reinterpret_cast<std::uint8_t *>(0x34100000U), 131072, 1, 65536, 2, graphics::Format::kRgb565
    };
    EXPECT_FALSE(dma::Dma2dDriver::ValidateRequest(request, 100));
    EXPECT_TRUE(hal.events.empty());
}

TEST_F(
    Dma2dTest,
    MutexTimeoutDoesNotTouchHardware
)
{
    hal.lock = E_TMOUT;
    EXPECT_EQ(dma::Dma2dManagement::Instance().Transfer(CopyRequest(), 40).Code(), Code::kTimeout);
    EXPECT_EQ(hal.lock_timeout, 40);
    EXPECT_TRUE(hal.events.empty());
}

TEST_F(
    Dma2dTest,
    InvalidOwnershipCannotOperateHardware
)
{
    dma::Dma2dManagement::Accessor accessor;
    ASSERT_TRUE(dma::Dma2dManagement::Instance().Acquire(&accessor).Ok());
    dma::Dma2dDriver::Writer invalid;
    EXPECT_EQ(accessor->Transfer(CopyRequest(), 100, invalid).Code(), Code::kOwnership);
    accessor->KeepClocksOnSleep(invalid);
    EXPECT_TRUE(hal.events.empty());
    accessor->KeepClocksOnSleep(accessor.Ownership());
    EXPECT_EQ(hal.events, (std::vector<std::string>{"sleep-clock"}));
}

TEST_F(
    Dma2dTest,
    HardwareFailuresStopOrResetAndAllowReuse
)
{
    for (unsigned failure = 0; failure < 4; ++failure) {
        SCOPED_TRACE(failure);
        hal = {};
        if (failure == 0)
            hal.initialize = HAL_ERROR;
        if (failure == 1)
            hal.configure = HAL_ERROR;
        if (failure == 2)
            hal.start = HAL_ERROR;
        if (failure == 3)
            hal.poll = HAL_ERROR;
        EXPECT_EQ(dma::Dma2dManagement::Instance().Transfer(CopyRequest()).Code(), Code::kHardware);
        const auto has = [](const char *event) {
            return std::find(hal.events.begin(), hal.events.end(), event) != hal.events.end();
        };
        EXPECT_EQ(has("reset"), failure < 2);
        EXPECT_EQ(has("abort"), failure >= 2);
        EXPECT_EQ(has("poll"), failure == 3);
        EXPECT_EQ(has("inspect"), failure >= 2);
        EXPECT_EQ(hal.events.back(), "unlock");
        hal = {};
        EXPECT_TRUE(dma::Dma2dManagement::Instance().Transfer(CopyRequest()).Ok());
    }
}

}

ID tk_cre_mtx(const T_CMTX *config)
{
    EXPECT_EQ(config->mtxatr, TA_INHERIT);
    return 1;
}

ER tk_loc_mtx(
    ID,
    TMO timeout
)
{
    hal.lock_timeout = timeout;
    return hal.lock;
}

ER tk_unl_mtx(ID)
{
    hal.events.emplace_back("unlock");
    return E_OK;
}

extern "C" {

void TestDma2dEvent(const char *event)
{
    hal.events.emplace_back(event);
}
void HAL_RIF_RISC_SetSlaveSecureAttributes(
    std::uint32_t,
    std::uint32_t
)
{}
void HAL_RIF_RIMC_ConfigMasterAttributes(
    std::uint32_t,
    const RIMC_MasterConfig_t *
)
{}

HAL_StatusTypeDef HAL_DMA2D_Init(DMA2D_HandleTypeDef *handle)
{
    hal.handle = *handle;
    hal.events.emplace_back("init");
    return hal.initialize;
}

HAL_StatusTypeDef HAL_DMA2D_ConfigLayer(
    DMA2D_HandleTypeDef *handle,
    std::uint32_t index
)
{
    hal.handle = *handle;
    hal.events.emplace_back(index == 1 ? "layer1" : "layer0");
    return hal.configure;
}

HAL_StatusTypeDef HAL_DMA2D_Start(
    DMA2D_HandleTypeDef *,
    std::uint32_t source,
    std::uint32_t destination,
    std::uint32_t width,
    std::uint32_t height
)
{
    hal.width = width;
    hal.height = height;
    hal.source = source;
    hal.destination = destination;
    hal.events.emplace_back("start");
    return hal.start;
}

HAL_StatusTypeDef HAL_DMA2D_BlendingStart(
    DMA2D_HandleTypeDef *handle,
    std::uint32_t source,
    std::uint32_t background,
    std::uint32_t destination,
    std::uint32_t width,
    std::uint32_t height
)
{
    hal.background = background;
    return HAL_DMA2D_Start(handle, source, destination, width, height);
}

HAL_StatusTypeDef HAL_DMA2D_PollForTransfer(
    DMA2D_HandleTypeDef *,
    std::uint32_t timeout
)
{
    hal.timeout = timeout;
    hal.events.emplace_back("poll");
    return hal.poll;
}

HAL_StatusTypeDef HAL_DMA2D_Abort(DMA2D_HandleTypeDef *)
{
    hal.events.emplace_back("abort");
    return hal.abort;
}

void SCB_CleanDCache_by_Addr(
    void *,
    std::int32_t bytes
)
{
    EXPECT_EQ(bytes, 64);
    hal.events.emplace_back("clean");
}

void SCB_CleanInvalidateDCache_by_Addr(
    void *,
    std::int32_t bytes
)
{
    EXPECT_EQ(bytes, 64);
    hal.events.emplace_back("prepare");
}

void SCB_InvalidateDCache_by_Addr(
    void *,
    std::int32_t bytes
)
{
    EXPECT_EQ(bytes, 64);
    hal.events.emplace_back("inspect");
}
}