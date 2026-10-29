/* FreeRTOS owns SysTick, so the HAL tick runs from TIM11 at 1 kHz instead. */
#include "board.h"

TIM_HandleTypeDef htim11;

HAL_StatusTypeDef HAL_InitTick(uint32_t priority)
{
    __HAL_RCC_TIM11_CLK_ENABLE();

    RCC_ClkInitTypeDef clk;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clk, &latency);
    uint32_t timer_clock = HAL_RCC_GetPCLK2Freq();
    if (clk.APB2CLKDivider != RCC_HCLK_DIV1) {
        timer_clock *= 2;
    }

    htim11.Instance = TIM11;
    htim11.Init.Prescaler = timer_clock / 1000000u - 1u;
    htim11.Init.Period = 1000u - 1u;
    htim11.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim11.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim11.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim11) != HAL_OK) {
        return HAL_ERROR;
    }

    HAL_NVIC_SetPriority(TIM1_TRG_COM_TIM11_IRQn, priority, 0);
    HAL_NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn);
    uwTickPrio = priority;
    return HAL_TIM_Base_Start_IT(&htim11);
}

void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&htim11, TIM_IT_UPDATE);
}

void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&htim11, TIM_IT_UPDATE);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM11) {
        HAL_IncTick();
    }
}
