#include "app.h"
#include "board.h"

extern TIM_HandleTypeDef htim11;

void NMI_Handler(void)
{
    for (;;) {
    }
}

void HardFault_Handler(void)
{
    Error_Handler();
}

void MemManage_Handler(void)
{
    Error_Handler();
}

void BusFault_Handler(void)
{
    Error_Handler();
}

void UsageFault_Handler(void)
{
    Error_Handler();
}

void DebugMon_Handler(void)
{
}

/* SVC, PendSV and SysTick belong to FreeRTOS (see FreeRTOSConfig.h). */

void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim11);
}

void USART1_IRQHandler(void)
{
    uplink_uart_rx_isr();
}
