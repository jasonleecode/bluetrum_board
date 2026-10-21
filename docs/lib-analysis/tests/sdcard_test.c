#include "drv_sdcard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define B(n) (1u << (n))
static uint32_t regs[0x700 / 4], csd[4], now, cmd_count[64], arg_seen[64];
static uint8_t dma[512] __attribute__((aligned(32))), written[512];
static int no_response_cmd, timeout_cmd, crc_cmd, wrong_index_cmd, r1_error_cmd;
static bool v2, hc, inserted, protected_socket, data_crc, data_timeout;
static bool write_rejected, stuck_busy, stuck_program, app_missing, bad_echo, ocr_wait;
static bool rx_armed, write_cmd, clear_response_with_cflag;
static unsigned checks, scenarios;
#define CHECK(x) do { ++checks; assert(x); } while (0)

static void field(unsigned start, unsigned count, uint32_t value)
{
    for (unsigned i = 0; i < count; i++) {
        unsigned bit = start + i, index = 3 - bit / 32;
        csd[index] = (csd[index] & ~B(bit % 32)) | (((value >> i) & 1) << (bit % 32));
    }
}
static void setup_csd(bool high_capacity)
{
    memset(csd, 0, sizeof(csd));
    field(126, 2, high_capacity ? 1 : 0);
    field(96, 8, 0x32); /* 25 MHz */
    field(84, 12, B(2) | B(4));
    field(80, 4, 9);
    if (high_capacity) field(48, 22, 8191); /* 4 GiB */
    else { field(62, 12, 1023); field(47, 3, 7); } /* 256 MiB */
}
static uint32_t micros(void *ctx) { (void)ctx; return now; }
static void delay(void *ctx, uint32_t us) { (void)ctx; now += us; }
static bool present(void *ctx) { (void)ctx; return inserted; }
static bool protect(void *ctx) { (void)ctx; return protected_socket; }
static drv_sdcard_cfg_t config(void)
{
    return (drv_sdcard_cfg_t){ .source_clock_hz=48000000,
        .time_us=micros, .delay_us=delay, .card_present=present,
        .write_protected=protect, .dma_buffer=dma, .dma_address=0x10000,
        .command_timeout_us=100, .data_timeout_us=1000 };
}
static void reset(void)
{
    ++scenarios;
    drv_sdcard_deinit();
    memset(regs, 0, sizeof(regs));
    memset(cmd_count, 0, sizeof(cmd_count));
    memset(arg_seen, 0, sizeof(arg_seen));
    no_response_cmd = timeout_cmd = crc_cmd = wrong_index_cmd = r1_error_cmd = -1;
    now = 0; v2 = hc = inserted = true;
    protected_socket = data_crc = data_timeout = write_rejected = stuck_busy = false;
    stuck_program = app_missing = bad_echo = ocr_wait = rx_armed = write_cmd = false;
    clear_response_with_cflag = false;
    setup_csd(true);
}
uint32_t drv_sdcard_test_read32(uint32_t address)
{
    assert(address < sizeof(regs) && !(address & 3));
    return regs[address / 4];
}
void drv_sdcard_test_write32(uint32_t address, uint32_t value)
{
    assert(address < sizeof(regs) && !(address & 3));
    if (address == 0x84) {
        regs[0x80/4] &= ~value;
        if (clear_response_with_cflag && (value & B(12)))
            regs[0x80/4] &= ~(B(14) | B(15));
        return;
    }
    regs[address/4] = value;
    if (address == 0x80 && !value) { rx_armed = write_cmd = false; return; }
    if (address == 0xa4) {
        assert(regs[0xa0/4] == 0x10000);
        assert((value & 0x7ff) == 512);
        if (value & B(16)) {
            assert(write_cmd);
            assert((value & (B(18)|B(17))) == (B(18)|B(17)));
            memcpy(written, dma, 512);
            if (!data_timeout) regs[0x80/4] |= B(13);
            regs[0x80/4] |= (write_rejected ? 5u : 2u) << 17;
            if (!stuck_busy) regs[0x80/4] |= B(20);
        } else rx_armed = true;
        return;
    }
    if (address != 0x8c) return;
    unsigned cmd = value & 0x3f;
    cmd_count[cmd]++;
    arg_seen[cmd] = regs[0x90/4];
    regs[0x80/4] &= 0xfff; /* hardware replaces command status */
    if ((int)cmd == timeout_cmd) return;
    regs[0x80/4] |= B(12) | B(20);
    regs[0x8c/4] = ((int)cmd == wrong_index_cmd) ? (cmd ^ 1) : cmd;
    if ((int)cmd == no_response_cmd || (cmd == 8 && !v2)) {
        regs[0x80/4] |= B(15); return;
    }
    if ((int)cmd == crc_cmd) regs[0x80/4] |= B(14);
    uint32_t reply = B(8) | (4u << 9);
    switch (cmd) {
    case 0: assert(!(value & B(8))); reply = 0; break;
    case 8: assert(arg_seen[8] == 0x1aa); reply = bad_echo ? 0x155 : 0x1aa; break;
    case 55: reply |= app_missing ? 0 : B(5); break;
    case 41:
        assert((arg_seen[41] & B(30)) == (v2 ? B(30) : 0));
        reply = (ocr_wait ? 0 : B(31)) | (hc ? B(30) : 0) | 0xff8000;
        regs[0x80/4] |= B(14); /* R3 CRC is intentionally ignored */
        break;
    case 3: reply = 0x12340000; break;
    case 9:
        assert(value & B(9));
        for (unsigned i=0; i<4; i++) regs[(0x90+i*4)/4] = csd[i];
        return;
    case 7: assert(value & B(10)); break;
    case 16: assert(arg_seen[16] == 512); break;
    case 17:
        assert(rx_armed);
        for (unsigned i=0; i<512; i++) dma[i] = (uint8_t)(i ^ arg_seen[17]);
        if (!data_timeout) regs[0x80/4] |= B(13);
        if (data_crc) regs[0x80/4] |= B(16);
        rx_armed = false;
        break;
    case 24: write_cmd = true; regs[0x80/4] &= ~B(20); break;
    case 13:
        if (write_cmd && stuck_program) reply = (7u << 9);
        break;
    }
    if ((int)cmd == r1_error_cmd) reply |= B(22);
    regs[0x90/4] = reply;
}
static void init_ok(void)
{
    drv_sdcard_cfg_t cfg = config();
    CHECK(drv_sdcard_init(&cfg));
    CHECK(drv_sdcard_last_error() == DRV_SDCARD_OK);
    CHECK((regs[0x80/4] & (B(1)|B(4)|B(5))) == 0); /* 1-bit, no interrupts */
    CHECK(regs[0x88/4] == 3); /* 12 MHz */
    CHECK((regs[0x64c/4] & 7) == 5); /* PB0/2 input, PB1 clock output */
}
static void init_bad(drv_sdcard_error_t expected)
{
    drv_sdcard_cfg_t cfg = config();
    CHECK(!drv_sdcard_init(&cfg));
    CHECK(drv_sdcard_last_error() == expected);
    CHECK(!(regs[0x80/4] & 1));
}
int main(void)
{
    uint8_t unaligned[514], input[513];
    drv_sdcard_info_t info;
    drv_sdcard_cfg_t cfg;
    reset(); init_ok();
    CHECK(drv_sdcard_get_info(&info));
    CHECK(info.block_count == 8388608 && info.block_addressing && info.rca == 0x1234);
    CHECK(cmd_count[16] == 0);
    CHECK(drv_sdcard_read_block(7, unaligned+1));
    CHECK(arg_seen[17] == 7);
    for (unsigned i=0; i<512; i++) CHECK(unaligned[i+1] == (uint8_t)(i ^ 7));
    for (unsigned i=0; i<512; i++) input[i+1] = (uint8_t)(i+3);
    CHECK(drv_sdcard_write_block(9, input+1));
    CHECK(arg_seen[24] == 9 && !memcmp(input+1, written, 512));
    CHECK(cmd_count[13] >= 4);
    CHECK(!drv_sdcard_read_block(8388608, unaligned));
    CHECK(drv_sdcard_last_error() == DRV_SDCARD_RANGE);
    CHECK(drv_sdcard_get_info(&info));
    CHECK(!drv_sdcard_read_block(0, NULL));
    CHECK(drv_sdcard_last_error() == DRV_SDCARD_ARGUMENT);
    CHECK(!drv_sdcard_read_block(0, dma)); CHECK(drv_sdcard_last_error() == DRV_SDCARD_ARGUMENT);
    cfg = config(); CHECK(!drv_sdcard_init(&cfg)); CHECK(drv_sdcard_last_error() == DRV_SDCARD_BUSY);
    protected_socket = true;
    CHECK(!drv_sdcard_write_block(0, input)); CHECK(drv_sdcard_last_error() == DRV_SDCARD_WRITE_PROTECTED);
    CHECK(drv_sdcard_get_info(&info) && info.write_protected);
    drv_sdcard_deinit(); CHECK(!drv_sdcard_read_block(0, unaligned));
    CHECK(drv_sdcard_last_error() == DRV_SDCARD_NOT_READY);

    for (unsigned legacy=0; legacy<2; legacy++) {
        reset(); hc = false; v2 = !legacy; setup_csd(false); init_ok();
        CHECK(drv_sdcard_get_info(&info));
        CHECK(info.block_count == 524288 && !info.block_addressing);
        CHECK(cmd_count[16] == 1);
        CHECK(drv_sdcard_read_block(511, unaligned+1)); CHECK(arg_seen[17] == 511*512);
        CHECK(drv_sdcard_write_block(200, input)); CHECK(arg_seen[24] == 200*512);
    }
    reset(); hc=false; v2=false; setup_csd(false); clear_response_with_cflag=true; init_ok();
    CHECK(drv_sdcard_get_info(&info) && !info.block_addressing);
    reset(); field(48,22,0x3fffff); init_ok();
    CHECK(drv_sdcard_get_info(&info) && info.block_count == (UINT64_C(1)<<32));
    CHECK(drv_sdcard_read_block(UINT32_MAX, unaligned)); CHECK(arg_seen[17] == UINT32_MAX);
    reset(); field(12,1,1); init_ok();
    CHECK(!drv_sdcard_write_block(0,input) && drv_sdcard_last_error()==DRV_SDCARD_WRITE_PROTECTED);
    reset(); field(84,12,B(2)); init_ok();
    CHECK(!drv_sdcard_write_block(0,input) && drv_sdcard_last_error()==DRV_SDCARD_UNSUPPORTED);

    reset(); data_crc=true; init_ok(); memset(unaligned,0x79,sizeof(unaligned));
    CHECK(!drv_sdcard_read_block(0,unaligned)); CHECK(drv_sdcard_last_error()==DRV_SDCARD_DATA_CRC);
    for (unsigned i=0;i<sizeof(unaligned);i++) CHECK(unaligned[i]==0x79);
    CHECK(!(regs[0x80/4]&1));
    reset(); data_timeout=true; init_ok();
    CHECK(!drv_sdcard_read_block(0,unaligned) && drv_sdcard_last_error()==DRV_SDCARD_TIMEOUT);
    reset(); write_rejected=true; init_ok();
    CHECK(!drv_sdcard_write_block(0,input) && drv_sdcard_last_error()==DRV_SDCARD_WRITE_REJECTED);
    reset(); stuck_busy=true; init_ok();
    CHECK(!drv_sdcard_write_block(0,input) && drv_sdcard_last_error()==DRV_SDCARD_TIMEOUT);
    reset(); stuck_program=true; init_ok();
    CHECK(!drv_sdcard_write_block(0,input) && drv_sdcard_last_error()==DRV_SDCARD_TIMEOUT);
    reset(); init_ok(); crc_cmd=17;
    CHECK(!drv_sdcard_read_block(0,unaligned) && drv_sdcard_last_error()==DRV_SDCARD_COMMAND_CRC);
    CHECK(!rx_armed && !(regs[0x80/4]&1));
    reset(); init_ok(); r1_error_cmd=24;
    CHECK(!drv_sdcard_write_block(0,input) && drv_sdcard_last_error()==DRV_SDCARD_RESPONSE);
    reset(); init_ok(); inserted=false;
    CHECK(!drv_sdcard_read_block(0,unaligned) && drv_sdcard_last_error()==DRV_SDCARD_NO_CARD);
    CHECK(!(regs[0x80/4]&1));
    reset(); now=UINT32_MAX-40100; init_ok(); data_timeout=true;
    CHECK(!drv_sdcard_read_block(0,unaligned) && drv_sdcard_last_error()==DRV_SDCARD_TIMEOUT);

    reset(); bad_echo=true; init_bad(DRV_SDCARD_UNSUPPORTED);
    reset(); app_missing=true; init_bad(DRV_SDCARD_UNSUPPORTED);
    reset(); timeout_cmd=8; init_bad(DRV_SDCARD_TIMEOUT); CHECK(cmd_count[55]==0);
    reset(); crc_cmd=8; init_bad(DRV_SDCARD_COMMAND_CRC); CHECK(cmd_count[55]==0);
    reset(); wrong_index_cmd=8; init_bad(DRV_SDCARD_RESPONSE);
    reset(); no_response_cmd=55; init_bad(DRV_SDCARD_RESPONSE);
    reset(); r1_error_cmd=7; init_bad(DRV_SDCARD_RESPONSE);
    reset(); crc_cmd=9; init_bad(DRV_SDCARD_COMMAND_CRC);
    reset(); ocr_wait=true; init_bad(DRV_SDCARD_TIMEOUT);
    reset(); field(126,2,2); init_bad(DRV_SDCARD_UNSUPPORTED);
    reset(); hc=false; init_bad(DRV_SDCARD_UNSUPPORTED); /* CSD/OCR mismatch */
    reset(); field(96,8,0); init_bad(DRV_SDCARD_UNSUPPORTED);
    reset(); field(84,12,B(4)); init_bad(DRV_SDCARD_UNSUPPORTED);
    reset(); inserted=false; init_bad(DRV_SDCARD_NO_CARD);
    reset(); cfg=config(); cfg.dma_address=0x20000000;
    CHECK(!drv_sdcard_init(&cfg) && drv_sdcard_last_error()==DRV_SDCARD_DMA_BUFFER);
    CHECK(cmd_count[0]==0);
    cfg=config(); cfg.dma_buffer=dma+1;
    CHECK(!drv_sdcard_init(&cfg) && drv_sdcard_last_error()==DRV_SDCARD_DMA_BUFFER);
    cfg=config(); cfg.source_clock_hz=120000000;
    CHECK(!drv_sdcard_init(&cfg) && drv_sdcard_last_error()==DRV_SDCARD_UNSUPPORTED);
    cfg=config(); cfg.source_clock_hz=100;
    CHECK(!drv_sdcard_init(&cfg) && drv_sdcard_last_error()==DRV_SDCARD_UNSUPPORTED);
    cfg=config(); cfg.time_us=NULL;
    CHECK(!drv_sdcard_init(&cfg) && drv_sdcard_last_error()==DRV_SDCARD_ARGUMENT);
    cfg=config(); cfg.command_timeout_us=5000001;
    CHECK(!drv_sdcard_init(&cfg) && drv_sdcard_last_error()==DRV_SDCARD_ARGUMENT);
    reset(); cfg=config(); cfg.transfer_clock_hz=UINT32_MAX;
    CHECK(drv_sdcard_init(&cfg)); CHECK(drv_sdcard_get_info(&info) && info.clock_hz==24000000);
    reset(); field(96,8,0x2a); cfg=config(); /* 20 MHz */
    cfg.transfer_clock_hz=24000000;
    CHECK(drv_sdcard_init(&cfg)); CHECK(drv_sdcard_get_info(&info) && info.clock_hz==16000000);
    reset(); cfg=config(); cfg.transfer_clock_hz=1;
    CHECK(!drv_sdcard_init(&cfg) && drv_sdcard_last_error()==DRV_SDCARD_UNSUPPORTED);
    printf("SD0 hardware model: %u scenarios, %u assertions passed (not hardware/differential tests)\n",scenarios,checks);
    return 0;
}
