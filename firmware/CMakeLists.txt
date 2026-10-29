cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
project(stm32_logger C ASM)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

set(TP ${CMAKE_CURRENT_LIST_DIR}/../third_party)
set(LIB ${CMAKE_CURRENT_LIST_DIR}/../lib)

set(HAL_MODULES hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_dma hal_i2c hal_adc hal_adc_ex
                hal_spi hal_uart hal_rtc hal_rtc_ex hal_iwdg hal_tim hal_tim_ex hal_pwr hal_pwr_ex
                hal_flash hal_flash_ex)
list(TRANSFORM HAL_MODULES PREPEND ${TP}/hal/Src/stm32f4xx_)
list(TRANSFORM HAL_MODULES APPEND .c)

add_library(vendor STATIC
  ${HAL_MODULES}
  ${TP}/cmsis-device/Source/Templates/system_stm32f4xx.c
  ${TP}/freertos/tasks.c
  ${TP}/freertos/queue.c
  ${TP}/freertos/list.c
  ${TP}/freertos/timers.c
  ${TP}/freertos/event_groups.c
  ${TP}/freertos/stream_buffer.c
  ${TP}/freertos/portable/GCC/ARM_CM4F/port.c
  ${TP}/freertos/portable/MemMang/heap_4.c
  ${TP}/fatfs/source/ff.c
)
target_include_directories(vendor PUBLIC
  Core/Inc
  ${TP}/hal/Inc
  ${TP}/cmsis-device/Include
  ${TP}/cmsis-core/Include
  ${TP}/freertos/include
  ${TP}/freertos/portable/GCC/ARM_CM4F
  ${TP}/fatfs/source
)
target_compile_definitions(vendor PUBLIC STM32F411xE USE_HAL_DRIVER HSE_VALUE=25000000U)
target_compile_options(vendor PRIVATE -w)

add_executable(firmware.elf
  ${TP}/cmsis-device/Source/Templates/gcc/startup_stm32f411xe.s
  Core/Src/main.c
  Core/Src/stm32f4xx_it.c
  Core/Src/hal_timebase.c
  Core/Src/rtc_time.c
  Core/Src/sensors.c
  Core/Src/logger.c
  Core/Src/uplink.c
  Core/Src/supervisor.c
  Core/Src/sd_spi.c
  ${LIB}/bme280.c
  ${LIB}/lm35.c
  ${LIB}/civil_time.c
  ${LIB}/record.c
)
target_include_directories(firmware.elf PRIVATE ${LIB})
target_compile_options(firmware.elf PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(firmware.elf PRIVATE vendor)
target_link_options(firmware.elf PRIVATE
  -T${CMAKE_CURRENT_LIST_DIR}/STM32F411CEUX_FLASH.ld
  -Wl,-Map=firmware.map)

add_custom_command(TARGET firmware.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary firmware.elf firmware.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex firmware.elf firmware.hex
  COMMAND ${CMAKE_SIZE} firmware.elf)
