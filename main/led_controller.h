#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "driver/gpio.h"
#include <stdbool.h>

// Пины для светодиодов (ESP32-S3)
#define LED_FORWARD_GPIO     GPIO_NUM_2
#define LED_BACKWARD_GPIO    GPIO_NUM_1
#define LED_LEFT_GPIO        GPIO_NUM_4
#define LED_RIGHT_GPIO       GPIO_NUM_5
#define LED_HORN_GPIO        GPIO_NUM_16   // Гудок (звуковой сигнал)
#define LED_LIGHTS_GPIO      GPIO_NUM_17   // Фары (основной свет)
#define LED_RADIO_GPIO       GPIO_NUM_18   // Рация (индикатор)

// Команды от Android
typedef enum {
    CMD_UNKNOWN = -1, 
    CMD_STOP = 0,
    CMD_FORWARD,
    CMD_BACKWARD,
    CMD_LEFT,
    CMD_RIGHT,
    CMD_HORN_ON,
    CMD_HORN_OFF,
    CMD_LIGHTS_ON,
    CMD_LIGHTS_OFF,
    CMD_RADIO_ON,
    CMD_RADIO_OFF
} command_t;

// Инициализация светодиодов
void led_controller_init(void);

// Управление светодиодами
void led_controller_execute(command_t cmd);

// Тестовые функции
void led_controller_test_sequence(int count, int delay_ms);
void led_controller_blink_all(int count, int delay_ms);
void led_controller_blink_forward_backward(int count, int delay_ms);
void led_controller_all_off(void);
void led_controller_all_on(void);

#endif // LED_CONTROLLER_H