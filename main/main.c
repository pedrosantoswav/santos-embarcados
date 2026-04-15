/*
 * =========================================================
 * Projeto 3
 * Sistemas Embarcados
 * 
 * Autor: Pedro Henrique Silva dos Santos
 * Data: 10/04/2026
 * Versão: 0.3.0
 *
 * Descrição:
 * Exibe as informações do processador e controla LED através de switch buttons
 * 
 * =========================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "driver/gptimer.h"

// =================== DEFINIÇÕES ===================

// Botões
#define B0 21
#define B1 22
#define B2 23
#define GPIO_INPUT_PIN_SEL ((1ULL<<B0) | (1ULL<<B1) | (1ULL<<B2))

// LED
#define LED 2
#define GPIO_OUTPUT_PIN_SEL (1ULL<<LED)

// TAGs
static const char *TAG_SYS  = "INFO_ESP";
static const char *TAG_GPIO = "GPIO";
static const char *TAG_TIMER = "TIMER_RT";

// Filas
static QueueHandle_t gpio_evt_queue = NULL;
static QueueHandle_t timer_evt_queue = NULL;

// =================== TIPOS ===================

typedef struct {
    uint64_t count;
    uint64_t alarm;
} timer_event_t;

typedef struct {
    uint32_t hora;
    uint32_t minuto;
    uint32_t segundo;
} relogio_t;

// =================== ISR ===================

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

static bool IRAM_ATTR timer_callback(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;

    timer_event_t evt = {
        .count = edata->count_value,
        .alarm = edata->alarm_value
    };

    xQueueSendFromISR(queue, &evt, &high_task_awoken);

    // próximo alarme (100 ms)
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->alarm_value + 100000
    };

    gptimer_set_alarm_action(timer, &alarm_config);

    return (high_task_awoken == pdTRUE);
}

// =================== TASKs ===================

// LED
static void led_control(void* arg)
{
    uint32_t io_num;
    int led_state = 0;

    while (1)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            int level = gpio_get_level(io_num);

            ESP_LOGI(TAG_GPIO, "GPIO[%" PRIu32 "] pressionado | Nivel: %d", io_num, level);

            if (io_num == B0)
            {
                gpio_set_level(LED, 1);
                led_state = 1;
            }
            else if (io_num == B1)
            {
                gpio_set_level(LED, 0);
                led_state = 0;
            }
            else if (io_num == B2)
            {
                led_state = !led_state;
                gpio_set_level(LED, led_state);

                ESP_LOGW(TAG_GPIO, "Botao 2 sem debounce");
            }
        }
    }
}

// TIMER
void timer_task(void *arg)
{
    timer_event_t evt;

    gptimer_handle_t timer = NULL;

    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&config, &timer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_callback,
    };

    // gora usa fila correta
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, timer_evt_queue));

    ESP_ERROR_CHECK(gptimer_enable(timer));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 100000
    };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));
    ESP_ERROR_CHECK(gptimer_start(timer));

    relogio_t relogio = {0, 0, 0};
    uint32_t contador_100ms = 0;

    while (1)
    {
        if (xQueueReceive(timer_evt_queue, &evt, portMAX_DELAY))
        {
            contador_100ms++;

            if (contador_100ms == 10)
            {
                contador_100ms = 0;

                relogio.segundo++;

                if (relogio.segundo == 60)
                {
                    relogio.segundo = 0;
                    relogio.minuto++;
                }

                if (relogio.minuto == 60)
                {
                    relogio.minuto = 0;
                    relogio.hora++;
                }

                if (relogio.hora == 24)
                {
                    relogio.hora = 0;
                }

                ESP_LOGI(TAG_TIMER,
                         "Hora: %02lu:%02lu:%02lu | Count: %llu | Alarm: %llu",
                         relogio.hora,
                         relogio.minuto,
                         relogio.segundo,
                         evt.count,
                         evt.alarm);
            }
        }
    }
}

// =================== MAIN ===================

void app_main(void)
{
    ESP_LOGI(TAG_SYS, "Iniciando programa...");

    // ===== INFO =====
    esp_chip_info_t chip_info;
    uint32_t flash_size;

    esp_chip_info(&chip_info);

    ESP_LOGI(TAG_SYS, "Chip: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG_SYS, "Nucleos: %d", chip_info.cores);

    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK)
    {
        ESP_LOGI(TAG_SYS, "Flash: %" PRIu32 " MB",
                 flash_size / (1024 * 1024));
    }

    ESP_LOGI(TAG_SYS, "ESP-IDF: %s", esp_get_idf_version());

    // ===== GPIO =====
    gpio_config_t io_conf;

    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io_conf);

    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    // ===== FILAS =====
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    timer_evt_queue = xQueueCreate(10, sizeof(timer_event_t));

    // ===== TASKS =====
    xTaskCreate(led_control, "led_control", 2048, NULL, 10, NULL);
    xTaskCreate(timer_task, "timer_task", 4096, NULL, 5, NULL);

    // ===== ISR =====
    gpio_install_isr_service(0);

    gpio_isr_handler_add(B0, gpio_isr_handler, (void*) B0);
    gpio_isr_handler_add(B1, gpio_isr_handler, (void*) B1);
    gpio_isr_handler_add(B2, gpio_isr_handler, (void*) B2);

    ESP_LOGI(TAG_GPIO, "Sistema pronto.");
}