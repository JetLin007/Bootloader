/**
 ******************************************************************************
 * @file    bootloader_main.c
 * @brief   OTA Bootloader（STM32F427IIH6，支持自动回滚）
 *
 * 启动逻辑：
 *   1. 检测主动回滚标志 → 回滚到备份区 → 清除标志 → 跳转
 *   2. 检测OTA升级标志 → 拷贝下载区到运行区 → 清除升级标志 → 跳转
 *      （待验证标志PENDING保留，App启动成功后自行清除）
 *   3. 检测待验证标志（无升级标志但PENDING仍在）→ 说明上次升级失败
 *      → 自动回滚到备份区 → 清除PENDING → 跳转
 *   4. 正常跳转App
 *
 * 防断电设计：
 *   - 拷贝过程中断电，重启后COPY_DONE未设置，重新拷贝
 *   - 拷贝完成后写COPY_DONE，再清除OTA_MAGIC
 *   - 回滚过程同样有COPY_DONE保护
 ******************************************************************************
 */
#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ============================================================
 * Bootloader版本信息（放在Flash固定地址0x0800FF00）
 * App可以通过读取这个地址获取Bootloader版本号
 * ============================================================ */
#define BL_VERSION_MAGIC  0x424C5652UL  /* "BLVR" */

typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 魔术字 0x424C5652 */
    char     version_str[16]; /* 版本字符串 "1.0.0" */
    uint8_t  major;           /* 主版本 */
    uint8_t  minor;           /* 次版本 */
    uint8_t  patch;           /* 修订号 */
    uint8_t  reserved;        /* 保留 */
    uint16_t crc;             /* CRC16（预留，暂为0） */
} bl_version_info_t;

/* 放在固定地址0x0800FF00的版本信息（由链接脚本保证地址） */
const bl_version_info_t g_bl_version_info
    __attribute__((section(".version_info"), used)) = {
    .magic       = BL_VERSION_MAGIC,
    .version_str = BL_VERSION,
    .major       = BL_VERSION_MAJOR,
    .minor       = BL_VERSION_MINOR,
    .patch       = BL_VERSION_PATCH,
    .reserved    = 0,
    .crc         = 0,
};

/* ==================== 分区定义（与ota.h一致） ==================== */
#define APP_RUN_ADDR         0x08010000UL
#define APP_RUN_SIZE         (448 * 1024)
#define APP_DL_ADDR          0x08080000UL
#define APP_DL_SIZE          (512 * 1024)
#define PARAM_ADDR           0x08100000UL
#define BACKUP_ADDR          0x08120000UL
#define BACKUP_SIZE          (512 * 1024)
#define OTA_MAGIC            0x55AA55AAUL
#define COPY_DONE_MAGIC      0xA55AA55AUL
#define PENDING_MAGIC        0x5A5A5A5AUL
#define ROLLBACK_MAGIC       0xA5A5A5A5UL
#define PARAM_MAGIC_OFF      0x0000
#define PARAM_FW_SIZE_OFF    0x0004
#define PARAM_FW_CRC_OFF     0x0008
#define PARAM_COPY_OFF       0x0010
#define PARAM_PENDING_OFF    0x0014
#define PARAM_BCK_SIZE_OFF   0x0018
#define PARAM_BCK_CRC_OFF    0x001C
#define PARAM_ROLLBACK_OFF   0x0020
#define PARAM_BCK_DONE_OFF   0x0024
#define BACKUP_DONE_MAGIC    0x42434B55UL  /* "BCKU" */

/* ==================== Flash扇区表 ==================== */
static const struct {
    uint32_t start;
    uint32_t size;
    uint8_t  sector;
} sector_table[] = {
    {0x08000000, 16*1024,  FLASH_SECTOR_0},
    {0x08004000, 16*1024,  FLASH_SECTOR_1},
    {0x08008000, 16*1024,  FLASH_SECTOR_2},
    {0x0800C000, 16*1024,  FLASH_SECTOR_3},
    {0x08010000, 64*1024,  FLASH_SECTOR_4},
    {0x08020000, 128*1024, FLASH_SECTOR_5},
    {0x08040000, 128*1024, FLASH_SECTOR_6},
    {0x08060000, 128*1024, FLASH_SECTOR_7},
    {0x08080000, 128*1024, FLASH_SECTOR_8},
    {0x080A0000, 128*1024, FLASH_SECTOR_9},
    {0x080C0000, 128*1024, FLASH_SECTOR_10},
    {0x080E0000, 128*1024, FLASH_SECTOR_11},
    {0x08100000, 128*1024, FLASH_SECTOR_12},
    {0x08120000, 128*1024, FLASH_SECTOR_13},
    {0x08140000, 128*1024, FLASH_SECTOR_14},
    {0x08160000, 128*1024, FLASH_SECTOR_15},
    {0x08180000, 128*1024, FLASH_SECTOR_16},
    {0x081A0000, 128*1024, FLASH_SECTOR_17},
    {0x081C0000, 128*1024, FLASH_SECTOR_18},
    {0x081E0000, 128*1024, FLASH_SECTOR_19},
};

static uint8_t addr_to_sector(uint32_t addr)
{
    for (uint32_t i = 0; i < sizeof(sector_table)/sizeof(sector_table[0]); i++)
    {
        if (addr >= sector_table[i].start &&
            addr < sector_table[i].start + sector_table[i].size)
            return sector_table[i].sector;
    }
    return FLASH_SECTOR_19;
}

/* ==================== Flash操作 ==================== */
static void flash_erase_range(uint32_t start_addr, uint32_t size)
{
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase;
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector       = addr_to_sector(start_addr);
    erase.NbSectors    = 0;
    uint32_t end = start_addr + size - 1;
    uint32_t cur = start_addr;
    while (cur <= end)
    {
        erase.NbSectors++;
        for (uint32_t i = 0; i < sizeof(sector_table)/sizeof(sector_table[0]); i++)
        {
            if (sector_table[i].start > cur) { cur = sector_table[i].start; break; }
        }
        if (cur > end) break;
    }
    uint32_t err = 0;
    HAL_FLASHEx_Erase(&erase, &err);
    HAL_FLASH_Lock();
}

static void flash_copy(uint32_t dst, uint32_t src, uint32_t size)
{
    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < size; i += 4)
    {
        uint32_t word = *(volatile uint32_t *)(src + i);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst + i, word);

        /* 每拷贝64KB喂一次狗 */
        if ((i & 0xFFFF) == 0)
        {
            /* Bootloader里用寄存器直接喂狗 */
            IWDG->KR = 0xAAAA;
        }
    }
    HAL_FLASH_Lock();
}

static void flash_write_word(uint32_t addr, uint32_t value)
{
    HAL_FLASH_Unlock();
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, value);
    HAL_FLASH_Lock();
}

/* ==================== CRC32（用于校验备份/新固件） ==================== */
static uint32_t crc32_table[256];
static int crc32_ready = 0;

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
        crc32_table[i] = crc;
    }
    crc32_ready = 1;
}

static uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    if (!crc32_ready) crc32_init();
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    return crc ^ 0xFFFFFFFF;
}

/* ==================== 回滚操作 ==================== */
static void do_rollback(void)
{
    uint32_t backup_size = *(volatile uint32_t *)(PARAM_ADDR + PARAM_BCK_SIZE_OFF);
    uint32_t backup_crc  = *(volatile uint32_t *)(PARAM_ADDR + PARAM_BCK_CRC_OFF);
    uint32_t backup_done = *(volatile uint32_t *)(PARAM_ADDR + PARAM_BCK_DONE_OFF);

    /* 校验备份完成标志（备份过程中断电时此标志不会被写入） */
    if (backup_done != BACKUP_DONE_MAGIC)
        return;

    /* 校验备份有效性 */
    if (backup_size == 0 || backup_size == 0xFFFFFFFF ||
        backup_size > BACKUP_SIZE)
        return;

    uint32_t calc_crc = crc32_calc((const uint8_t *)BACKUP_ADDR, backup_size);
    if (calc_crc != backup_crc)
        return;  /* 备份损坏，不回滚 */

    /* 擦除运行区 */
    flash_erase_range(APP_RUN_ADDR, APP_RUN_SIZE);

    /* 拷贝备份区到运行区 */
    flash_copy(APP_RUN_ADDR, BACKUP_ADDR, backup_size);

    /* 清除待验证和回滚标志（擦除参数区整个扇区） */
    flash_erase_range(PARAM_ADDR, 128 * 1024);
}

/* ==================== 升级操作 ==================== */
static void do_upgrade(void)
{
    uint32_t fw_size = *(volatile uint32_t *)(PARAM_ADDR + PARAM_FW_SIZE_OFF);
    uint32_t fw_crc  = *(volatile uint32_t *)(PARAM_ADDR + PARAM_FW_CRC_OFF);
    uint32_t copy_done = *(volatile uint32_t *)(PARAM_ADDR + PARAM_COPY_OFF);

    if (fw_size == 0 || fw_size == 0xFFFFFFFF || fw_size > APP_DL_SIZE)
        return;

    /* 如果拷贝未完成，执行拷贝 */
    if (copy_done != COPY_DONE_MAGIC)
    {
        /* 校验下载区CRC */
        uint32_t calc_crc = crc32_calc((const uint8_t *)APP_DL_ADDR, fw_size);
        if (calc_crc != fw_crc)
            return;  /* 下载区损坏，不升级 */

        /* 擦除运行区 */
        flash_erase_range(APP_RUN_ADDR, APP_RUN_SIZE);

        /* 拷贝下载区到运行区 */
        flash_copy(APP_RUN_ADDR, APP_DL_ADDR, fw_size);

        /* 写拷贝完成标志 */
        flash_write_word(PARAM_ADDR + PARAM_COPY_OFF, COPY_DONE_MAGIC);
    }

    /* 清除OTA升级标志（保留PENDING待验证标志） */
    flash_write_word(PARAM_ADDR + PARAM_MAGIC_OFF, 0);
}

/* ==================== 跳转到App ==================== */
typedef void (*app_entry_t)(void);

static void jump_to_app(void)
{
    uint32_t app_stack = *(volatile uint32_t *)APP_RUN_ADDR;
    if (app_stack < 0x20000000 || app_stack > 0x20030000)
        return;

    uint32_t app_reset = *(volatile uint32_t *)(APP_RUN_ADDR + 4);
    if (app_reset < APP_RUN_ADDR || app_reset >= 0x08100000)
        return;

    /* 关闭所有中断 */
    __disable_irq();

    /* 关闭SysTick（HAL_Init启动了SysTick，必须关闭否则跳转后会触发中断） */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 清除所有NVIC中断使能和挂起（共8个寄存器，覆盖64个中断） */
    for (uint32_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* 清除PendSV和SysTick挂起 */
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

    /* 设置栈指针并跳转到App */
    __set_MSP(app_stack);

    /* 数据同步屏障，确保上面的操作完成后再跳转 */
    __DSB();
    __ISB();

    app_entry_t app_entry = (app_entry_t)app_reset;
    app_entry();
}

/* ==================== 主函数 ==================== */
int main(void)
{

    /* 临时调试：先关闭所有中断和SysTick */
    __disable_irq();
    SysTick->CTRL = 0;

    HAL_Init();

    uint32_t ota_magic    = *(volatile uint32_t *)(PARAM_ADDR + PARAM_MAGIC_OFF);
    uint32_t pending      = *(volatile uint32_t *)(PARAM_ADDR + PARAM_PENDING_OFF);
    uint32_t rollback_req = *(volatile uint32_t *)(PARAM_ADDR + PARAM_ROLLBACK_OFF);


    /* 优先级1：主动回滚请求 */
    if (rollback_req == ROLLBACK_MAGIC)
    {
        do_rollback();
    }
    /* 优先级2：OTA升级 */
    else if (ota_magic == OTA_MAGIC)
    {
        do_upgrade();
        /* PENDING标志保留，App启动成功后自行清除 */
    }
    /* 优先级3：待验证但无升级标志 → 上次升级失败，自动回滚 */
    else if (pending == PENDING_MAGIC)
    {
        /* App没成功启动（没调用ota_mark_verify_success），自动回滚 */
        do_rollback();
    }

    /* 跳转到App */
    jump_to_app();

    /* 跳转失败则死循环 */
    while (1) { __NOP(); }
}
