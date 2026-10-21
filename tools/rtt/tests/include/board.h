/* Host MMIO fixture. The production build always uses board/board.h. */
#ifndef TEST_BOARD_H
#define TEST_BOARD_H
#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <stdint.h>
#define BIT(n) (1u << (n))
typedef volatile uint32_t *hal_sfr_t;
typedef int hal_error_t;
typedef int irq_type;
#define ADC_CHANNEL_0 BIT(0)
#define ADC_CHANNEL_1 BIT(1)
#define ADC_CHANNEL_2 BIT(2)
#define ADC_CHANNEL_3 BIT(3)
#define ADC_CHANNEL_4 BIT(4)
#define ADC_CHANNEL_5 BIT(5)
#define ADC_CHANNEL_6 BIT(6)
#define ADC_CHANNEL_7 BIT(7)
#define ADC_CHANNEL_8 BIT(8)
#define ADC_CHANNEL_9 BIT(9)
#define ADC_CHANNEL_10 BIT(10)
#define ADC_CHANNEL_11 BIT(11)
#define ADC_CHANNEL_12 BIT(12)
#define ADC_CHANNEL_13 BIT(13)
#define ADC_CHANNEL_14 BIT(14)
#define ADC_CHANNEL_15 BIT(15)
#define HAL_EOK 0
#define HAL_ETIMEOUT 2
#define RCU_SPI1 1
#define RCU_FMRX 15
#define RCU_IIS 30
uint32_t get_sysclk_nhz(void);
void hal_rcu_periph_clk_enable(uint32_t clock);
void hal_rcu_periph_clk_disable(uint32_t clock);
void hal_adc_enable(uint8_t enabled);
void hal_adc_start(uint32_t channel);
hal_error_t hal_adc_poll_for_conversion(uint32_t timeout);
extern volatile uint32_t test_spi[6], test_ports[4][9], test_timers[5][8];
extern volatile uint32_t test_wdt, test_clk, test_adc[16], test_func;
#define SPI1CON test_spi[0]
#define SPI1BUF test_spi[1]
#define SPI1BAUD test_spi[2]
#define SPI1CPND test_spi[3]
#define WDTCON test_wdt
#define CLKCON0 test_clk
#define SADCDAT0 test_adc[0]
#define FUNCMCON1 test_func
#define GPIOA_BASE test_ports[0]
#define GPIOB_BASE test_ports[1]
#define GPIOE_BASE test_ports[2]
#define GPIOF_BASE test_ports[3]
#define TIM1_BASE test_timers[0]
#define TIM2_BASE test_timers[1]
#define TIM3_BASE test_timers[2]
#define TIM4_BASE test_timers[3]
#define TIM5_BASE test_timers[4]
enum {TMRxCON, TMRxCPND, TMRxCNT, TMRxPR};
enum {IRQ_TMR1_VECTOR, IRQ_TMR2_4_5_VECTOR, IRQ_IRRX_VECTOR};

extern volatile uint32_t extra_regs[32];
#define PLL1CON extra_regs[0]
#define CLKCON2 extra_regs[1]
#define PLL1DIV extra_regs[2]
#define AUANGCON0 extra_regs[3]
#define AUANGCON1 extra_regs[4]
#define AUANGCON2 extra_regs[5]
#define AUANGCON3 extra_regs[6]
#define DACDIGCON0 extra_regs[7]
#define AU0LMIXCOEF extra_regs[8]
#define AU1LMIXCOEF extra_regs[9]
#define DACVOLCON extra_regs[10]
#define AUBUFSIZE extra_regs[11]
#define AUBUFSTARTADDR extra_regs[12]
#define AUBUFCON extra_regs[13]
#define AUBUFDATA extra_regs[14]
#define RTCCON extra_regs[15]
#define RTCDAT extra_regs[16]
#define PICCON extra_regs[17]
#define PICCONCLR extra_regs[18]
#define DMA_ADR(p) ((rt_ubase_t)(p))
#define IRQ_AUBUF0_1_VECTOR 9
#define RTC_WR 0x80
#define RTC_RD 0
#define RTCCNT_CMD 0x40
#define RTCCON0_CMD 0x00
#define RTCCON2_CMD 0x02
void hal_mdelay(uint32_t ms);
#include "ab32vg1_hal_rtc.h"
extern volatile uint32_t test_sd[10];
#define SDMMC0_BASE test_sd
#define RCU_SD0 2
enum {RCU_TMR1 = 24, RCU_TMR2, RCU_TMR3 = 34, RCU_TMR4, RCU_TMR5};
#define IRQ_SD_VECTOR 10
enum {SDxCON, SDxCPND, SDxBAUD, SDxCMD, SDxARG3, SDxARG2, SDxARG1, SDxARG0, SDxDMAADR, SDxDMACNT};
struct sd_handle { hal_sfr_t instance; };
void hal_sd_mspinit(struct sd_handle *hsd);
#endif
