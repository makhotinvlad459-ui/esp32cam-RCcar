#include "led_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LED_CTRL";

// Состояния для дополнительных функций
static bool horn_state = false;
static bool lights_state = false;
static bool radio_state = false;

void led_controller_init(void) {
    // Основные светодиоды движения
    gpio_reset_pin(LED_FORWARD_GPIO);
    gpio_reset_pin(LED_BACKWARD_GPIO);
    gpio_reset_pin(LED_LEFT_GPIO);
    gpio_reset_pin(LED_RIGHT_GPIO);
    
    // Дополнительные функции
    gpio_reset_pin(LED_HORN_GPIO);
    gpio_reset_pin(LED_LIGHTS_GPIO);
    gpio_reset_pin(LED_RADIO_GPIO);
    
    gpio_set_direction(LED_FORWARD_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_BACKWARD_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_LEFT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_RIGHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_HORN_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_LIGHTS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_RADIO_GPIO, GPIO_MODE_OUTPUT);
    
    led_controller_all_off();
}

void led_controller_execute(command_t cmd) {
    ESP_LOGI(TAG, "Executing command: %d", cmd);
    
    switch (cmd) {
        case CMD_FORWARD:
            ESP_LOGI(TAG, "FORWARD command executed");
            // Не гасим фары и т.д., только движение
            gpio_set_level(LED_FORWARD_GPIO, 1);
            gpio_set_level(LED_BACKWARD_GPIO, 0);
            gpio_set_level(LED_LEFT_GPIO, 0);
            gpio_set_level(LED_RIGHT_GPIO, 0);
            break;
            
        case CMD_BACKWARD:
            ESP_LOGI(TAG, "BACKWARD command executed");
            gpio_set_level(LED_FORWARD_GPIO, 0);
            gpio_set_level(LED_BACKWARD_GPIO, 1);
            gpio_set_level(LED_LEFT_GPIO, 0);
            gpio_set_level(LED_RIGHT_GPIO, 0);
            break;
            
        case CMD_LEFT:
            ESP_LOGI(TAG, "LEFT command executed");
            gpio_set_level(LED_FORWARD_GPIO, 0);
            gpio_set_level(LED_BACKWARD_GPIO, 0);
            gpio_set_level(LED_LEFT_GPIO, 1);
            gpio_set_level(LED_RIGHT_GPIO, 0);
            break;
            
        case CMD_RIGHT:
            ESP_LOGI(TAG, "RIGHT command executed");
            gpio_set_level(LED_FORWARD_GPIO, 0);
            gpio_set_level(LED_BACKWARD_GPIO, 0);
            gpio_set_level(LED_LEFT_GPIO, 0);
            gpio_set_level(LED_RIGHT_GPIO, 1);
            break;
            
        case CMD_STOP:
            ESP_LOGI(TAG, "STOP command executed");
            // Останавливаем движение, но оставляем фары и т.д.
            gpio_set_level(LED_FORWARD_GPIO, 0);
            gpio_set_level(LED_BACKWARD_GPIO, 0);
            gpio_set_level(LED_LEFT_GPIO, 0);
            gpio_set_level(LED_RIGHT_GPIO, 0);
            break;
            
        case CMD_HORN_ON:
            ESP_LOGI(TAG, "HORN_ON command executed");
            horn_state = true;
            // Гудок мигает или включается постоянно
            gpio_set_level(LED_HORN_GPIO, 1);
            break;
            
        case CMD_HORN_OFF:
            ESP_LOGI(TAG, "HORN_OFF command executed");
            horn_state = false;
            gpio_set_level(LED_HORN_GPIO, 0);
            break;
            
        case CMD_LIGHTS_ON:
            ESP_LOGI(TAG, "LIGHTS_ON command executed");
            lights_state = true;
            gpio_set_level(LED_LIGHTS_GPIO, 1);
            break;
            
        case CMD_LIGHTS_OFF:
            ESP_LOGI(TAG, "LIGHTS_OFF command executed");
            lights_state = false;
            gpio_set_level(LED_LIGHTS_GPIO, 0);
            break;
            
        case CMD_RADIO_ON:
            ESP_LOGI(TAG, "RADIO_ON command executed");
            radio_state = true;
            // Рация мигает
            gpio_set_level(LED_RADIO_GPIO, 1);
            break;
            
        case CMD_RADIO_OFF:
            ESP_LOGI(TAG, "RADIO_OFF command executed");
            radio_state = false;
            gpio_set_level(LED_RADIO_GPIO, 0);
            break;
        
         case CMD_UNKNOWN:  // ← ДОБАВЬ ЭТОТ case
            ESP_LOGW(TAG, "Получена неизвестная команда");
            break;
        default:
            ESP_LOGW(TAG, "Непредвиденная команда: %d", cmd);
            break;    
    }
}

void led_controller_test_sequence(int count, int delay_ms) {
    command_t commands[] = {CMD_FORWARD, CMD_BACKWARD, CMD_LEFT, CMD_RIGHT, CMD_STOP,
                            CMD_HORN_ON, CMD_HORN_OFF, CMD_LIGHTS_ON, CMD_LIGHTS_OFF,
                            CMD_RADIO_ON, CMD_RADIO_OFF};
    int num_commands = sizeof(commands) / sizeof(commands[0]);
    
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < num_commands; j++) {
            led_controller_execute(commands[j]);
            vTaskDelay(delay_ms / portTICK_PERIOD_MS);
        }
    }
}

void led_controller_blink_all(int count, int delay_ms) {
    for (int i = 0; i < count; i++) {
        led_controller_all_on();
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
        led_controller_all_off();
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
    }
}

void led_controller_blink_forward_backward(int count, int delay_ms) {
    for (int i = 0; i < count; i++) {
        gpio_set_level(LED_FORWARD_GPIO, 1);
        gpio_set_level(LED_BACKWARD_GPIO, 1);
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
        gpio_set_level(LED_FORWARD_GPIO, 0);
        gpio_set_level(LED_BACKWARD_GPIO, 0);
        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
    }
}

void led_controller_all_off(void) {
    gpio_set_level(LED_FORWARD_GPIO, 0);
    gpio_set_level(LED_BACKWARD_GPIO, 0);
    gpio_set_level(LED_LEFT_GPIO, 0);
    gpio_set_level(LED_RIGHT_GPIO, 0);
    gpio_set_level(LED_HORN_GPIO, 0);
    gpio_set_level(LED_LIGHTS_GPIO, 0);
    gpio_set_level(LED_RADIO_GPIO, 0);
    
    horn_state = false;
    lights_state = false;
    radio_state = false;
}

void led_controller_all_on(void) {
    gpio_set_level(LED_FORWARD_GPIO, 1);
    gpio_set_level(LED_BACKWARD_GPIO, 1);
    gpio_set_level(LED_LEFT_GPIO, 1);
    gpio_set_level(LED_RIGHT_GPIO, 1);
    gpio_set_level(LED_HORN_GPIO, 1);
    gpio_set_level(LED_LIGHTS_GPIO, 1);
    gpio_set_level(LED_RADIO_GPIO, 1);
}