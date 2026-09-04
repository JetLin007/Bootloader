cmake_minimum_required(VERSION 3.16)

# ==================== 项目配置 ====================
project(Rehab_Bootloader C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ==================== 芯片定义 ====================
add_compile_definitions(
    STM32F427xx
    USE_HAL_DRIVER
)

# ==================== 编译选项 ====================
add_compile_options(
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    -O0
    -g3
    -gdwarf-4
)

# ==================== 链接选项 ====================
add_link_options(
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -specs=nano.specs
    -specs=nosys.specs
    -T${CMAKE_SOURCE_DIR}/STM32F427IIHX_FLASH.ld
    -Wl,-Map=${PROJECT_NAME}.map,--cref
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)

# ==================== 自动收集源文件 ====================
file(GLOB_RECURSE SOURCES
    "${CMAKE_SOURCE_DIR}/Core/Src/*.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/*.c"
)

# 启动文件（汇编）
list(APPEND SOURCES "${CMAKE_SOURCE_DIR}/startup_stm32f427xx.s")

# ==================== 头文件路径 ====================
include_directories(
    Core/Inc
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
    Drivers/STM32F4xx_HAL_Driver/Inc
)

# ==================== 生成目标 ====================
add_executable(${PROJECT_NAME}.elf ${SOURCES})

# ==================== 生成bin和hex ====================
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    COMMENT "Generating bin and hex"
)