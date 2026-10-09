#include "commands.hpp"
#include "tests/suite.hpp"
#include <tk/tkernel.h>

extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"
void tm_com_init(void);
}

namespace {

alignas(8) INT task_stack[16 * 1024 / sizeof(INT)];

void Write(void *, const char *text, std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index) {
        tm_putchar(static_cast<unsigned char>(text[index]));
    }
}

const experiment::console::Writer output{nullptr, Write};

void TestTask(INT, void *)
{
    experiment::hwtest::Registry registry{experiment::hwtest::tests::cases,
                                         experiment::hwtest::tests::case_count,
                                         {HAL_GetTick, [](std::uint32_t delay) { tk_dly_tsk(delay); }}};
    const experiment::console::Command commands[] = {
        {"hwtest", "hwtest list|all|run <name> [allow-destructive]", experiment::hwtest::Execute, &registry}
    };
    experiment::console::Shell shell(commands, sizeof(commands) / sizeof(commands[0]), output);
    output.Write("HWTEST READY\n> ");
    bool discard_line = false;
    for (;;) {
        if ((USART1->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0) {
            USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
            discard_line = true;
        }
        if ((USART1->ISR & USART_ISR_RXNE_RXFNE) != 0) {
            const auto character = static_cast<char>(USART1->RDR & 0xffU);
            if (discard_line) {
                if (character == '\r' || character == '\n') {
                    shell.Feed('\x03');
                    output.Write("ERR uart-receive; line discarded\n> ");
                    discard_line = false;
                }
            } else {
                shell.Feed(character);
            }
        } else {
            tk_dly_tsk(1);
        }
    }
}

}

extern "C" INT usermain(void)
{
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) { return E_SYS; }
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

    T_CTSK task{};
    task.tskatr = TA_HLNG | TA_USERBUF;
    task.task = reinterpret_cast<FP>(TestTask);
    task.itskpri = 10;
    task.stksz = sizeof(task_stack);
    task.bufptr = task_stack;
    const ID identifier = tk_cre_tsk(&task);
    if (identifier < E_OK || tk_sta_tsk(identifier, 0) != E_OK) {
        output.Write("hwtest: task creation failed\n");
        return E_SYS;
    }
    for (;;) { tk_slp_tsk(TMO_FEVR); }
}