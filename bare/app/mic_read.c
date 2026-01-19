#include "drv_adc.h"
#include "drv_uart.h"
#include <stdio.h>
#include <string.h>

#define SAMPLE_RATE 16000
#define BUF_SIZE 256
#define THRESHOLD 500    // 音量阈值
#define DOT_DURATION 200 // ms
#define DASH_DURATION 600
#define LETTER_SPACE 600
#define WORD_SPACE 1400

int16_t adc_buf[BUF_SIZE];

typedef struct {
    const char *morse;
    char letter;
} morse_map_t;

static const morse_map_t morse_table[] = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},
    {".", 'E'}, {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'},
    {"..", 'I'}, {".---", 'J'}, {"-.-", 'K'}, {".-..", 'L'},
    {"--", 'M'}, {"-.", 'N'}, {"---", 'O'}, {".--.", 'P'},
    {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
    {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'},
    {"-.--", 'Y'}, {"--..", 'Z'},
    {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'},
    {"....-", '4'}, {".....", '5'}, {"-....", '6'}, {"--...", '7'},
    {"---..", '8'}, {"----.", '9'},
};

static void uart_print_char(char c) {
    drv_uart_putchar(c);
}

static char morse_decode(const char *morse) {
    for(int i=0; i<sizeof(morse_table)/sizeof(morse_table[0]); i++){
        if(strcmp(morse_table[i].morse, morse)==0)
            return morse_table[i].letter;
    }
    return '?';
}

int main(void)
{
    drv_uart_init(115200);

    drv_adc_cfg_t cfg = {
        .channel = DRV_ADC_CH_MIC,
        .sample_rate = SAMPLE_RATE,
        .gain = 8
    };
    drv_adc_init(&cfg);
    drv_adc_start();

    char current_morse[16] = {0};
    int morse_len = 0;

    uint32_t time_counter = 0; // 毫秒计数

    while(1)
    {
        uint32_t n = drv_adc_read(adc_buf, BUF_SIZE);
        if(n==0) continue;

        // 计算简单绝对平均
        int32_t sum = 0;
        for(uint32_t i=0;i<n;i++) sum += (adc_buf[i]>0?adc_buf[i]:-adc_buf[i]);
        int32_t avg = sum / n;

        bool sound = (avg > THRESHOLD);
        uint32_t duration_ms = (n * 1000) / SAMPLE_RATE;

        time_counter += duration_ms;

        static bool last_sound = false;
        static uint32_t sound_time = 0;
        static uint32_t silence_time = 0;

        if(sound) {
            sound_time += duration_ms;
            if(!last_sound) silence_time = time_counter;
            last_sound = true;
        } else {
            silence_time += duration_ms;
            if(last_sound) {
                // 上一个音符结束
                if(sound_time > DASH_DURATION) current_morse[morse_len++] = '-';
                else current_morse[morse_len++] = '.';
                sound_time = 0;
            }
            last_sound = false;

            // 检测字间隔
            if(silence_time >= WORD_SPACE) {
                if(morse_len>0){
                    current_morse[morse_len]=0;
                    uart_print_char(morse_decode(current_morse));
                    uart_print_char(' ');
                    morse_len=0;
                }
                silence_time = 0;
            } else if(silence_time >= LETTER_SPACE) {
                if(morse_len>0){
                    current_morse[morse_len]=0;
                    uart_print_char(morse_decode(current_morse));
                    morse_len=0;
                }
            }
        }
    }
}