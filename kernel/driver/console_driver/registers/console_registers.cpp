#include "driver/console_driver/console_driver.hpp"
#include "driver/console_driver/rx_queue.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>
void tm_com_init(void);
}

namespace {
uai::ai::console::RxQueue received;
uai::ai::console::Notifier notification{};
}

namespace uai::ai::console::registers {
common::Error ConsoleRegisterLayer::Initialize(Notifier notifier)
{
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
        return {common::ErrorCode::kHardware};
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOE, &gpio);
    tm_com_init();
    notification = notifier;
    HAL_NVIC_SetPriority(USART1_IRQn, 14, 0);
    SET_BIT(USART1->CR1, USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE);
    SET_BIT(USART1->CR3, USART_CR3_EIE);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    return {};
}
common::Error ConsoleRegisterLayer::Read(Input &input)
{
    return received.Pop(input.value, input.error) ? common::Error{} : common::Error{common::ErrorCode::kNoFrame};
}
common::Error ConsoleRegisterLayer::Write(
    const char *text,
    std::size_t size
)
{
    for (std::size_t index = 0; index < size; ++index)
        tm_putchar(static_cast<unsigned char>(text[index]));
    return {};
}
}

extern "C" void USART1_IRQHandler(void)
{
    for (unsigned budget = 0; budget < 64; ++budget) {
        const auto flags = USART1->ISR;
        if ((flags & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0) {
            USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
            received.Error();
            if ((flags & USART_ISR_RXNE_RXFNE) != 0)
                (void)USART1->RDR;
            continue;
        }
        if ((flags & USART_ISR_RXNE_RXFNE) == 0)
            break;
        received.Push(static_cast<char>(USART1->RDR & 0xffU));
    }
    if (notification.wake != nullptr)
        notification.wake(notification.context);
}