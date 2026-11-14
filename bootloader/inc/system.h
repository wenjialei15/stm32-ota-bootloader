#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

#define SYSTEM_CLOCK_HZ 64000000u

/*
 * HSI 8 MHz / 2 * 16 = 64 MHz。
 * 刻意不用 HSE：Blue Pill 上的 8 MHz 晶振质量参差，起振失败会卡在
 * 等 HSERDY 的死循环里，出现"上电没反应"这种最难查的故障。
 */
void clock_init(void);

/* PC13 推挽输出，Blue Pill 板载 LED（低电平点亮） */
void led_init(void);
void led_on(void);
void led_off(void);
void led_toggle(void);

/* 粗略忙等，不追求精度，只用于闪灯节奏 */
void delay_cycles(uint32_t cycles);

#endif /* SYSTEM_H */
