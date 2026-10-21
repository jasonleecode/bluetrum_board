/* Native SD0 hardware driver reconstructed from local primary documentation:
 * AB32VG1_Register.pdf chapter 9; rtt/.../hal_drivers/drv_sdio.c;
 * rtt/components/drivers/sdio/dev_sd.c. This is a new implementation, not an
 * equivalence replacement for a vendor archive object. See SD_RECOVERY.md. */
#include "drv_sdcard.h"
#include <stddef.h>

#define BIT(n) (1u << (n))
enum {
    SD_CON = 0x80, SD_CPND = 0x84, SD_BAUD = 0x88, SD_CMD = 0x8c,
    SD_ARG3 = 0x90, SD_ARG2 = 0x94, SD_ARG1 = 0x98, SD_ARG0 = 0x9c,
    SD_DMAADR = 0xa0, SD_DMACNT = 0xa4,
    MAP0 = 0x1c, CLKGAT0 = 0x3b0, PB_DIR = 0x64c, PB_DE = 0x650,
    PB_FEN = 0x654, PB_PU = 0x65c, PB_PD = 0x660,
    PB_PU200 = 0x664, PB_PD200 = 0x668, PB_PU300 = 0x66c, PB_PD300 = 0x670,
    CFLAG = BIT(12), DFLAG = BIT(13), CCRCE = BIT(14), NRPS = BIT(15),
    DCRCE = BIT(16), NOT_BUSY = BIT(20),
    RSP = BIT(8), LONG_RSP = BIT(9), CMD_BUSY = BIT(10), CLK8 = BIT(11),
    R1_ERRORS = 0xffff8008u, /* card status errors, excludes ECC_DISABLED/ERASE_RESET */
    APP_CMD = BIT(5), READY = BIT(8), TRAN = 4u << 9,
};

#ifdef DRV_SDCARD_TEST
extern uint32_t drv_sdcard_test_read32(uint32_t address);
extern void drv_sdcard_test_write32(uint32_t address, uint32_t value);
#define read32 drv_sdcard_test_read32
#define write32 drv_sdcard_test_write32
#else
static uint32_t read32(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)address;
}
static void write32(uint32_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
}
#endif

static drv_sdcard_cfg_t sd;
static drv_sdcard_info_t card;
static drv_sdcard_error_t error;
static bool owned, ready;
static bool writable;
static bool last_no_response;

static void fence_dma(void)
{
#ifdef __riscv
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}
static bool fail(drv_sdcard_error_t why)
{
    error = why;
    return false;
}
static bool abort_transfer(void)
{
    /* SDEN=0 stops the controller. Keep the dedicated staging buffer alive
     * through deinit/reinit; never hand an in-flight DMA a caller stack buffer. */
    if (owned) {
        write32(SD_CON, 0);
        fence_dma();
        write32(SD_CPND, CFLAG | DFLAG);
    }
    ready = false;
    return false;
}
static bool present(void)
{
    return !sd.card_present || sd.card_present(sd.context);
}
static bool wait_flag(uint32_t mask, uint32_t timeout, uint32_t *status)
{
    uint32_t start = sd.time_us(sd.context);
    do {
        if (!present()) return fail(DRV_SDCARD_NO_CARD);
        *status = read32(SD_CON);
        if ((*status & mask) == mask) return true;
        sd.delay_us(sd.context, 1);
    } while ((uint32_t)(sd.time_us(sd.context) - start) < timeout);
    return fail(DRV_SDCARD_TIMEOUT);
}
/* R3 has no valid CRC; R2 has no echoed command index. */
static bool command(uint8_t cmd, uint32_t arg, uint32_t flags, bool crc)
{
    uint32_t status;
    last_no_response = false;
    write32(SD_CPND, CFLAG);
    write32(SD_ARG3, arg);
    write32(SD_CMD, cmd | 0x40u | CLK8 | flags);
    if (!wait_flag(CFLAG, (flags & CMD_BUSY) ? sd.data_timeout_us :
                   sd.command_timeout_us, &status)) return false;
    write32(SD_CPND, CFLAG);
    if (!(flags & RSP)) return true;
    if (status & NRPS) {
        last_no_response = true;
        return fail(DRV_SDCARD_RESPONSE);
    }
    if (crc && (status & CCRCE)) return fail(DRV_SDCARD_COMMAND_CRC);
    if (crc && !(flags & LONG_RSP) && (read32(SD_CMD) & 0x3fu) != cmd)
        return fail(DRV_SDCARD_RESPONSE);
    return true;
}
static bool r1_command(uint8_t cmd, uint32_t arg, bool busy)
{
    if (!command(cmd, arg, RSP | (busy ? CMD_BUSY : 0), true)) return false;
    if (read32(SD_ARG3) & R1_ERRORS) return fail(DRV_SDCARD_RESPONSE);
    return true;
}
static bool app_command(uint32_t arg)
{
    if (!r1_command(55, 0, false)) return false;
    if (!(read32(SD_ARG3) & APP_CMD)) return fail(DRV_SDCARD_UNSUPPORTED);
    return command(41, arg, RSP, false);
}
static bool set_clock(uint32_t target)
{
    uint32_t divisor = sd.source_clock_hz / target;
    if (sd.source_clock_hz % target) divisor++;
    if (divisor < 2) divisor = 2; /* SD0BAUD must not be zero. */
    if (divisor > 256) return fail(DRV_SDCARD_UNSUPPORTED);
    write32(SD_BAUD, divisor - 1);
    card.clock_hz = sd.source_clock_hz / divisor;
    return true;
}
static uint32_t csd_bits(const uint32_t csd[4], uint8_t start, uint8_t width)
{
    uint8_t index = (uint8_t)(3u - start / 32u), shift = start % 32u;
    uint32_t value = csd[index] >> shift;
    if (width + shift > 32) value |= csd[index - 1] << (32u - shift);
    return value & ((1u << width) - 1u);
}
static bool parse_csd(const uint32_t csd[4], uint32_t *max_hz)
{
    uint32_t structure = csd_bits(csd, 126, 2);
    if (structure == 1 && card.block_addressing) {
        card.block_count = ((uint64_t)csd_bits(csd, 48, 22) + 1u) << 10;
    } else if (structure == 0 && !card.block_addressing) {
        uint32_t length = csd_bits(csd, 80, 4);
        if (length < 9 || length > 11) return fail(DRV_SDCARD_UNSUPPORTED);
        card.block_count = ((uint64_t)csd_bits(csd, 62, 12) + 1u) <<
            (csd_bits(csd, 47, 3) + 2u + length - 9u);
        /* Byte addresses in CMD17/24 must fit in 32 bits. */
        if (card.block_count > (UINT64_C(1) << 23))
            return fail(DRV_SDCARD_UNSUPPORTED);
    } else return fail(DRV_SDCARD_UNSUPPORTED);
    if (!(csd_bits(csd, 84, 12) & BIT(2))) return fail(DRV_SDCARD_UNSUPPORTED);
    writable = (csd_bits(csd, 84, 12) & BIT(4)) != 0;
    card.write_protected = csd_bits(csd, 12, 2) != 0;
    static const uint8_t mantissa[16] = {0,10,12,13,15,20,25,30,35,40,45,50,55,60,70,80};
    static const uint32_t units[4] = {10000,100000,1000000,10000000};
    uint32_t rate = csd_bits(csd, 96, 8);
    if ((rate & 0x80u) || (rate & 7u) > 3 || !mantissa[(rate >> 3) & 15u])
        return fail(DRV_SDCARD_UNSUPPORTED);
    *max_hz = units[rate & 7u] * mantissa[(rate >> 3) & 15u];
    return true;
}
static bool wait_card_ready(void)
{
    uint32_t start = sd.time_us(sd.context);
    do {
        if (!r1_command(13, (uint32_t)card.rca << 16, false)) return false;
        uint32_t state = read32(SD_ARG3);
        if ((state & (READY | 0x1e00u)) == (READY | TRAN)) return true;
        sd.delay_us(sd.context, 100);
    } while ((uint32_t)(sd.time_us(sd.context) - start) < sd.data_timeout_us);
    return fail(DRV_SDCARD_TIMEOUT);
}
static void configure_pins(void)
{
    /* FUNCMCON fields are write commands: 0xf clears a mapping, 2 selects G2.
     * Do not read/modify/write: unrelated fields could be written as commands. */
    write32(MAP0, 0xf);
    write32(MAP0, 2);
    write32(PB_DIR, (read32(PB_DIR) | 5u) & ~2u);
    write32(PB_DE, read32(PB_DE) | 7u);
    write32(PB_PU, read32(PB_PU) | 5u);
    write32(PB_PD, read32(PB_PD) & ~7u);
    write32(PB_PU200, read32(PB_PU200) & ~7u);
    write32(PB_PD200, read32(PB_PD200) & ~7u);
    write32(PB_PU300, read32(PB_PU300) & ~7u);
    write32(PB_PD300, read32(PB_PD300) & ~7u);
    write32(PB_FEN, read32(PB_FEN) | 7u);
}

bool drv_sdcard_init(const drv_sdcard_cfg_t *cfg)
{
    if (ready) return fail(DRV_SDCARD_BUSY);
    if (!cfg || !cfg->source_clock_hz || !cfg->time_us || !cfg->delay_us ||
        cfg->command_timeout_us > 5000000u || cfg->data_timeout_us > 5000000u)
        return fail(DRV_SDCARD_ARGUMENT);
    if (!cfg->dma_buffer || ((uintptr_t)cfg->dma_buffer & 31u) ||
        (cfg->dma_address & 31u) || cfg->dma_address < 0x1000u ||
        cfg->dma_address > 0x200000u - DRV_SDCARD_BLOCK_SIZE)
        return fail(DRV_SDCARD_DMA_BUFFER);
    /* Validate identification divisor before touching hardware. */
    if (cfg->source_clock_hz < 200000u || cfg->source_clock_hz > 102400000u)
        return fail(DRV_SDCARD_UNSUPPORTED);
    sd = *cfg;
    if (!sd.command_timeout_us) sd.command_timeout_us = 100000u;
    if (!sd.data_timeout_us) sd.data_timeout_us = 1000000u;
    card = (drv_sdcard_info_t){0};
    writable = false;
    error = DRV_SDCARD_OK;
    if (!present()) return fail(DRV_SDCARD_NO_CARD);
    owned = true;
    write32(CLKGAT0, read32(CLKGAT0) | BIT(9) | BIT(17)); /* SD0 and GPIO */
    write32(SD_CON, 0);
    sd.delay_us(sd.context, 20);
    configure_pins();
    if (!set_clock(400000u)) return abort_transfer();
    write32(SD_CPND, CFLAG | DFLAG);
    write32(SD_CON, BIT(0) | BIT(3)); /* 1-bit, continuous clock, polling */
    sd.delay_us(sd.context, 40000);
    if (!command(0, 0, 0, false)) return abort_transfer();
    bool v2 = command(8, 0x1aau, RSP, true);
    if (v2) {
        if (read32(SD_ARG3) != 0x1aau) {
            fail(DRV_SDCARD_UNSUPPORTED);
            return abort_transfer();
        }
    } else if (error != DRV_SDCARD_RESPONSE || !last_no_response) {
        /* Only a completed CMD8 with no response identifies a legacy card.
         * Controller timeout, CRC failure, and malformed replies are errors. */
        return abort_transfer();
    }
    error = DRV_SDCARD_OK;
    uint32_t start = sd.time_us(sd.context), ocr;
    for (;;) {
        if (!app_command(0x00ff8000u | (v2 ? BIT(30) : 0))) return abort_transfer();
        ocr = read32(SD_ARG3);
        if (!(ocr & 0x00ff8000u)) {
            fail(DRV_SDCARD_UNSUPPORTED);
            return abort_transfer();
        }
        if (ocr & BIT(31)) break;
        if ((uint32_t)(sd.time_us(sd.context) - start) >= 1000000u) {
            fail(DRV_SDCARD_TIMEOUT);
            return abort_transfer();
        }
        sd.delay_us(sd.context, 1000);
    }
    card.block_addressing = v2 && (ocr & BIT(30));
    if (!command(2, 0, RSP | LONG_RSP, true) || !command(3, 0, RSP, true))
        return abort_transfer();
    uint32_t r6 = read32(SD_ARG3);
    card.rca = (uint16_t)(r6 >> 16);
    if (!card.rca || (r6 & 0xe000u)) {
        fail(DRV_SDCARD_RESPONSE);
        return abort_transfer();
    }
    if (!command(9, (uint32_t)card.rca << 16, RSP | LONG_RSP, true))
        return abort_transfer();
    uint32_t csd[4] = {read32(SD_ARG3), read32(SD_ARG2), read32(SD_ARG1), read32(SD_ARG0)};
    uint32_t max_hz;
    if (!parse_csd(csd, &max_hz) || !r1_command(7, (uint32_t)card.rca << 16, true))
        return abort_transfer();
    if (!card.block_addressing && !r1_command(16, DRV_SDCARD_BLOCK_SIZE, false))
        return abort_transfer();
    if (!wait_card_ready()) return abort_transfer();
    uint32_t target = sd.transfer_clock_hz ? sd.transfer_clock_hz : 12000000u;
    if (target > 25000000u) target = 25000000u;
    if (target > max_hz) target = max_hz;
    if (!set_clock(target)) return abort_transfer();
    /* Keep clocks running for DAT0 busy release; no IRQ registration needed. */
    ready = true;
    error = DRV_SDCARD_OK;
    return true;
}

void drv_sdcard_deinit(void)
{
    abort_transfer();
    owned = false;
    sd = (drv_sdcard_cfg_t){0};
    card = (drv_sdcard_info_t){0};
    error = DRV_SDCARD_OK;
}
drv_sdcard_error_t drv_sdcard_last_error(void) { return error; }
bool drv_sdcard_get_info(drv_sdcard_info_t *info)
{
    if (!info) return fail(DRV_SDCARD_ARGUMENT);
    if (!ready) return fail(DRV_SDCARD_NOT_READY);
    if (!present()) { fail(DRV_SDCARD_NO_CARD); return abort_transfer(); }
    *info = card;
    if (sd.write_protected && sd.write_protected(sd.context)) info->write_protected = true;
    error = DRV_SDCARD_OK;
    return true;
}
static bool validate_io(uint32_t block, const void *buffer)
{
    if (!buffer) return fail(DRV_SDCARD_ARGUMENT);
    if (!ready) return fail(DRV_SDCARD_NOT_READY);
    if (!present()) { fail(DRV_SDCARD_NO_CARD); return abort_transfer(); }
    uintptr_t caller = (uintptr_t)buffer, staging = (uintptr_t)sd.dma_buffer;
    if ((caller >= staging && caller - staging < DRV_SDCARD_BLOCK_SIZE) ||
        (staging >= caller && staging - caller < DRV_SDCARD_BLOCK_SIZE))
        return fail(DRV_SDCARD_ARGUMENT);
    if ((uint64_t)block >= card.block_count) return fail(DRV_SDCARD_RANGE);
    error = DRV_SDCARD_OK;
    return true;
}
static uint32_t block_argument(uint32_t block)
{
    return card.block_addressing ? block : block << 9;
}
bool drv_sdcard_read_block(uint32_t block, uint8_t *buf512)
{
    uint32_t status;
    if (!validate_io(block, buf512)) return false;
    if (!wait_card_ready()) return abort_transfer();
    write32(SD_CPND, DFLAG);
    write32(SD_DMAADR, sd.dma_address);
    fence_dma();
    write32(SD_DMACNT, DRV_SDCARD_BLOCK_SIZE); /* RX armed before CMD17 */
    if (!r1_command(17, block_argument(block), false) ||
        !wait_flag(DFLAG, sd.data_timeout_us, &status)) return abort_transfer();
    write32(SD_CPND, DFLAG);
    if (status & DCRCE) { fail(DRV_SDCARD_DATA_CRC); return abort_transfer(); }
    fence_dma();
    for (uint32_t i = 0; i < DRV_SDCARD_BLOCK_SIZE; i++) buf512[i] = sd.dma_buffer[i];
    return true;
}
bool drv_sdcard_write_block(uint32_t block, const uint8_t *buf512)
{
    uint32_t status;
    if (!validate_io(block, buf512)) return false;
    if (!writable) return fail(DRV_SDCARD_UNSUPPORTED);
    if (card.write_protected || (sd.write_protected && sd.write_protected(sd.context)))
        return fail(DRV_SDCARD_WRITE_PROTECTED);
    if (!wait_card_ready()) return abort_transfer();
    for (uint32_t i = 0; i < DRV_SDCARD_BLOCK_SIZE; i++) sd.dma_buffer[i] = buf512[i];
    write32(SD_CPND, DFLAG);
    if (!r1_command(24, block_argument(block), false)) return abort_transfer();
    write32(SD_DMAADR, sd.dma_address);
    fence_dma();
    write32(SD_DMACNT, BIT(18) | BIT(17) | BIT(16) | DRV_SDCARD_BLOCK_SIZE);
    if (!wait_flag(DFLAG, sd.data_timeout_us, &status)) return abort_transfer();
    write32(SD_CPND, DFLAG);
    if (((status >> 17) & 7u) != 2) {
        fail(DRV_SDCARD_WRITE_REJECTED);
        return abort_transfer();
    }
    if (!wait_flag(NOT_BUSY, sd.data_timeout_us, &status) || !wait_card_ready())
        return abort_transfer();
    return true;
}
