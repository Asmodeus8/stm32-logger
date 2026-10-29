/* microSD over SPI, implementing the FatFs disk interface (diskio.h). */
#include <stdbool.h>

#include "board.h"
#include "diskio.h"

#define CMD0   0
#define CMD8   8
#define CMD9   9
#define CMD12  12
#define CMD16  16
#define CMD17  17
#define CMD18  18
#define CMD24  24
#define CMD25  25
#define CMD55  55
#define CMD58  58
#define ACMD41 (0x80 | 41)

enum { CARD_NONE = 0, CARD_V1 = 1, CARD_V2 = 2, CARD_BLOCK = 4 };

static volatile DSTATUS status = STA_NOINIT;
static uint8_t card_type;

static void cs_low(void)  { HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET); }
static void cs_high(void) { HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET); }

static uint8_t xfer(uint8_t out)
{
    uint8_t in = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &out, &in, 1, 10);
    return in;
}

static void set_speed(uint32_t prescaler)
{
    __HAL_SPI_DISABLE(&hspi1);
    MODIFY_REG(hspi1.Instance->CR1, SPI_CR1_BR, prescaler);
    __HAL_SPI_ENABLE(&hspi1);
}

static bool wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    do {
        if (xfer(0xFF) == 0xFF) {
            return true;
        }
    } while (HAL_GetTick() - start < timeout_ms);
    return false;
}

static void deselect(void)
{
    cs_high();
    xfer(0xFF); /* one extra clock so the card releases MISO */
}

static bool select(void)
{
    cs_low();
    xfer(0xFF);
    if (wait_ready(500)) {
        return true;
    }
    deselect();
    return false;
}

static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
    if (cmd & 0x80) { /* ACMDn is CMD55 followed by CMDn */
        cmd &= 0x7F;
        uint8_t r = send_cmd(CMD55, 0);
        if (r > 1) {
            return r;
        }
    }
    if (cmd != CMD12) {
        deselect();
        if (!select()) {
            return 0xFF;
        }
    }

    xfer((uint8_t)(0x40 | cmd));
    xfer((uint8_t)(arg >> 24));
    xfer((uint8_t)(arg >> 16));
    xfer((uint8_t)(arg >> 8));
    xfer((uint8_t)arg);
    /* CRC is only checked for CMD0 and CMD8 in SPI mode. */
    xfer(cmd == CMD0 ? 0x95 : cmd == CMD8 ? 0x87 : 0x01);
    if (cmd == CMD12) {
        xfer(0xFF);
    }

    uint8_t r;
    int n = 10;
    do {
        r = xfer(0xFF);
    } while ((r & 0x80) && --n);
    return r;
}

static bool read_block(uint8_t *buf, UINT len)
{
    uint32_t start = HAL_GetTick();
    uint8_t token;
    do {
        token = xfer(0xFF);
    } while (token == 0xFF && HAL_GetTick() - start < 200);
    if (token != 0xFE) {
        return false;
    }
    for (UINT i = 0; i < len; i++) {
        buf[i] = xfer(0xFF);
    }
    xfer(0xFF); /* CRC */
    xfer(0xFF);
    return true;
}

static bool write_block(const uint8_t *buf, uint8_t token)
{
    if (!wait_ready(500)) {
        return false;
    }
    xfer(token);
    if (token == 0xFD) { /* stop-transmission token carries no data */
        return true;
    }
    for (UINT i = 0; i < 512; i++) {
        xfer(buf[i]);
    }
    xfer(0xFF);
    xfer(0xFF);
    return (xfer(0xFF) & 0x1F) == 0x05; /* data accepted */
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) {
        return STA_NOINIT;
    }
    set_speed(SPI_BAUDRATEPRESCALER_256);
    cs_high();
    for (int i = 0; i < 10; i++) { /* at least 74 clocks with CS high */
        xfer(0xFF);
    }

    uint8_t type = CARD_NONE;
    if (send_cmd(CMD0, 0) == 1) {
        uint32_t start = HAL_GetTick();
        if (send_cmd(CMD8, 0x1AA) == 1) {
            uint8_t ocr[4];
            for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                while (HAL_GetTick() - start < 1000 && send_cmd(ACMD41, 1UL << 30) != 0) {
                }
                if (HAL_GetTick() - start < 1000 && send_cmd(CMD58, 0) == 0) {
                    for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
                    type = (ocr[0] & 0x40) ? (CARD_V2 | CARD_BLOCK) : CARD_V2;
                }
            }
        } else {
            uint8_t cmd = send_cmd(ACMD41, 0) <= 1 ? ACMD41 : CMD55; /* SDv1, else MMC is not supported */
            if (cmd == ACMD41) {
                while (HAL_GetTick() - start < 1000 && send_cmd(ACMD41, 0) != 0) {
                }
                if (HAL_GetTick() - start < 1000 && send_cmd(CMD16, 512) == 0) {
                    type = CARD_V1;
                }
            }
        }
    }
    deselect();

    card_type = type;
    if (type == CARD_NONE) {
        status = STA_NOINIT;
    } else {
        set_speed(SPI_BAUDRATEPRESCALER_8); /* 12.5 MHz */
        status = 0;
    }
    return status;
}

DSTATUS disk_status(BYTE pdrv)
{
    return pdrv == 0 ? status : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || count == 0) return RES_PARERR;
    if (status & STA_NOINIT) return RES_NOTRDY;
    uint32_t addr = (card_type & CARD_BLOCK) ? (uint32_t)sector : (uint32_t)sector * 512u;

    if (count == 1) {
        if (send_cmd(CMD17, addr) == 0 && read_block(buff, 512)) {
            count = 0;
        }
    } else if (send_cmd(CMD18, addr) == 0) {
        do {
            if (!read_block(buff, 512)) break;
            buff += 512;
        } while (--count);
        send_cmd(CMD12, 0);
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || count == 0) return RES_PARERR;
    if (status & STA_NOINIT) return RES_NOTRDY;
    uint32_t addr = (card_type & CARD_BLOCK) ? (uint32_t)sector : (uint32_t)sector * 512u;

    if (count == 1) {
        if (send_cmd(CMD24, addr) == 0 && write_block(buff, 0xFE)) {
            count = 0;
        }
    } else if (send_cmd(CMD25, addr) == 0) {
        do {
            if (!write_block(buff, 0xFC)) break;
            buff += 512;
        } while (--count);
        if (!write_block(NULL, 0xFD)) {
            count = 1;
        }
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) return RES_PARERR;
    if (status & STA_NOINIT) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC: {
        bool ok = select();
        deselect();
        return ok ? RES_OK : RES_ERROR;
    }
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
