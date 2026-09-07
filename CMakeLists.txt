cmake_minimum_required(VERSION 3.16)

# ==================== 项目配置 ====================
project(Rehab_Bootloader C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ==================== Bootloader版本号（唯一修改位置） ====================
set(BL_VERSION "1.0.0")
string(REPLACE "." ";" VERSION_LIST ${BL_VERSION})
list(GET VERSION_LIST 0 BL_VERSION_MAJOR)
list(GET VERSION_LIST 1 BL_VERSION_MINOR)
list(GET VERSION_LIST 2 BL_VERSION_PATCH)

add_compile_definitions(
    BL_VERSION="${BL_VERSION}"
    BL_VERSION_MAJOR=${BL_VERSION_MAJOR}
    BL_VERSION_MINOR=${BL_VERSION_MINOR}
    BL_VERSION_PATCH=${BL_VERSION_PATCH}
    STM32F427xx
    USE_HAL_DRIVER
)

# ==================== 编译选项（强制-Os优化大小） ====================
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")

add_compile_options(
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -Wall
    -ffunction-sections
    -fdata-sections
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

# ==================== 源文件（最精简，只包含必需文件） ====================
# 只编译main.c和system_stm32f4xx.c
# 不编译：gpio.c, hal_msp.c, it.c, syscalls.c, sysmem.c
set(CORE_SOURCES
    "${CMAKE_SOURCE_DIR}/Core/Src/main.c"
    "${CMAKE_SOURCE_DIR}/Core/Src/system_stm32f4xx.c"
    "${CMAKE_SOURCE_DIR}/Core/Src/stm32f4xx_it.c"
)

# 只包含最必需的HAL模块（Flash操作+HAL核心）
set(HAL_SOURCES
    "${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c"
)

# 启动文件
set(STARTUP_ASM "${CMAKE_SOURCE_DIR}/startup_stm32f427xx.s")

set(ALL_SOURCES
    ${CORE_SOURCES}
    ${HAL_SOURCES}
    ${STARTUP_ASM}
)

# ==================== 头文件路径 ====================
include_directories(
    Core/Inc
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
    Drivers/STM32F4xx_HAL_Driver/Inc
)

# ==================== 生成目标 ====================
add_executable(${PROJECT_NAME}.elf ${ALL_SOURCES})

# ==================== 生成bin和hex ====================
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}_v${BL_VERSION}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   ${PROJECT_NAME}.elf ${PROJECT_NAME}_v${BL_VERSION}.hex
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    COMMENT "Generating ${PROJECT_NAME}_v${BL_VERSION}.bin / .hex"
)

# ==================== OpenOCD一键烧录（与主工程一致） ====================
# 烧录地址：Bootloader=0x08000000，App固件=0x08010000
set(FLASH_ADDRESS "0x08000000")
set(BIN_FILE ${CMAKE_BINARY_DIR}/${PROJECT_NAME}_v${BL_VERSION}.bin)

add_custom_target(flash
    COMMAND openocd
        -f interface/stlink.cfg
        -c "transport select swd"
        -c "adapter speed 4000"
        -f target/stm32f4x.cfg
        -c "program ${BIN_FILE} ${FLASH_ADDRESS} verify reset exit"
    DEPENDS ${PROJECT_NAME}.elf
)
