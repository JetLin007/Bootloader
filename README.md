# Rehab_Bootloader 工程说明

## 目录结构

```
Rehab_Bootloader/
├── CMakeLists.txt              # 构建脚本（已提供）
├── STM32F427IIHX_FLASH.ld      # 链接脚本（已提供，Flash=64KB）
├── startup_stm32f427xx.s        # 启动文件（从CubeMX工程复制）
├── Core/
│   ├── Inc/
│   │   └── main.h              # 可选，空文件即可
│   └── Src/
│       ├── main.c              # Bootloader主程序（已提供，即bootloader_main.c）
│       ├── system_stm32f4xx.c  # 系统时钟配置（从CubeMX工程复制）
│       └── syscalls.c          # 系统调用（从CubeMX工程复制）
└── Drivers/
    ├── CMSIS/
    │   ├── Include/
    │   │   ├── core_cm4.h
    │   │   ├── core_cm4_simd.h
    │   │   └── cmsis_armcc.h 等
    │   └── Device/ST/STM32F4xx/Include/
    │       ├── stm32f427xx.h
    │       ├── stm32f4xx.h
    │       └── system_stm32f4xx.h
    └── STM32F4xx_HAL_Driver/
        ├── Inc/
        │   ├── stm32f4xx_hal.h
        │   ├── stm32f4xx_hal_conf.h  # 需要配置
        │   ├── stm32f4xx_hal_def.h
        │   ├── stm32f4xx_hal_flash.h
        │   ├── stm32f4xx_hal_flash_ex.h
        │   ├── stm32f4xx_hal_rcc.h
        │   ├── stm32f4xx_hal_rcc_ex.h
        │   ├── stm32f4xx_hal_pwr.h
        │   ├── stm32f4xx_hal_pwr_ex.h
        │   ├── stm32f4xx_hal_gpio.h
        │   ├── stm32f4xx_hal_cortex.h
        │   └── stm32f4xx_hal_exti.h
        └── Src/
            ├── stm32f4xx_hal.c
            ├── stm32f4xx_hal_cortex.c
            ├── stm32f4xx_hal_rcc.c
            ├── stm32f4xx_hal_rcc_ex.c
            ├── stm32f4xx_hal_pwr.c
            ├── stm32f4xx_hal_pwr_ex.c
            ├── stm32f4xx_hal_gpio.c
            ├── stm32f4xx_hal_flash.c
            └── stm32f4xx_hal_flash_ex.c
```

## 最快搭建方式（推荐）

不用手动建目录，直接用CubeMX生成基础工程，然后替换3个文件：

### 步骤1：CubeMX生成基础工程

1. 新建工程，芯片选 `STM32F427IIHx`
2. 时钟用内部HSI（不用配外部晶振）
3. 不配置任何外设
4. 不启用FreeRTOS
5. Toolchain选 `CMake`
6. 生成代码

### 步骤2：替换/修改3个文件

| 文件 | 操作 |
|------|------|
| `Core/Src/main.c` | 用 `bootloader_main.c` 整个替换 |
| `STM32F427IIHX_FLASH.ld` | 用提供的链接脚本替换（Flash大小改为64K） |
| `CMakeLists.txt` | 用提供的CMakeLists替换（精简HAL模块，只保留必要的） |

### 步骤3：确认stm32f4xx_hal_conf.h

CubeMX生成的 `stm32f4xx_hal_conf.h` 里会启用所有HAL模块，Bootloader不需要那么多。可以不改，编译会大一点但不影响功能。如果想精简，只保留这些模块的宏定义：
```c
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
```

## 编译

```bash
# 配置
cmake -B build -G Ninja

# 编译
cmake --build build

# 输出文件在 build/ 目录：
#   Rehab_Bootloader.elf
#   Rehab_Bootloader.bin   ← 烧录用这个
#   Rehab_Bootloader.hex
```

## 烧录

```bash
# ST-Link烧录到0x08000000
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "program build/Rehab_Bootloader.bin 0x08000000 verify reset exit"
```

或用ST-Link Utility：
- 连接ST-Link
- 打开 `Rehab_Bootloader.bin`
- 起始地址填 `0x08000000`
- 点击Program

## 验证

1. 烧录Bootloader后，再烧录App到 `0x08010000`
2. 复位，观察串口输出
3. 如果App正常启动，说明Bootloader跳转成功
4. 如果App区为空，Bootloader会死循环（正常）

## 预期大小

Bootloader编译后约 **8-15KB**，远小于64KB分区。

## 注意事项

1. **不要在Bootloader里配置CAN/UART等外设**，不需要
2. **不要启用FreeRTOS**，Bootloader是裸机程序
3. **时钟用HSI就行**，16MHz足够Flash操作
4. **链接脚本Flash必须是64KB**，不能用默认的2MB
5. **启动文件用CubeMX生成的** `startup_stm32f427xx.s`，不要自己写
6. **syscalls.c** 用CubeMX生成的，提供_write等弱符号
