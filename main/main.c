/*
 * =========================================================
 * Projeto 4
 * Sistemas Embarcados
 * 
 * Autor: Pedro Henrique Silva dos Santos
 * Data: 28/04/2026
 * Versão: 0.4.0
 *
 * Features:
 * - Informações detalhadas do sistema
 * - (desativado) Controle de LED via botões
 * - Relógio digital
 * - Controle de PWM com modos automático e manual (Controle LED atualizado)
 * 
 * =========================================================
 */

#include <stdio.h>              // Biblioteca padrão de entrada/saída
#include <string.h>             // Suporte às Strings
#include <stdlib.h>             // Alocacação de Memória
#include <inttypes.h>           // Definições de tipos inteiros com tamanho fixo
#include <stdbool.h>            // Suporte ao tipo booleano (true/false)

#include "sdkconfig.h"          // Configurações do projeto ESP-IDF

#include "freertos/FreeRTOS.h"  // Kernel do FreeRTOS
#include "freertos/task.h"      // Manipulação de tarefas
#include "freertos/queue.h"     // Filas
#include "freertos/semphr.h"    // Semáforos

#include "driver/gpio.h"        // Configurações pinos GPIO
#include "driver/gptimer.h"     // Configurações TIMER
#include "driver/ledc.h"        // Configurações PWM (LEDC)

#include "esp_chip_info.h"      // Informações sobre o chip ESP
#include "esp_flash.h"          // Funções relacionadas à memória flash
#include "esp_system.h"         // Funções gerais do sistema
#include "esp_log.h"            // Sistema de logging (ESP_LOGI, ESP_LOGE, etc.)
#include "esp_idf_version.h"    // Versão do ESP-IDF

// =================== DEFINIÇÕES ===================

#define B0 21
#define B1 22
#define B2 23
#define GPIO_INPUT_PIN_SEL ((1ULL<<B0) | (1ULL<<B1) | (1ULL<<B2))

// PWM GPIOs
#define PWM_LED_GPIO   16
#define PWM_SCOPE_GPIO 33

/*
// LED (antigo)
#define LED 2
#define GPIO_OUTPUT_PIN_SEL (1ULL<<LED)
*/

// TAGs
static const char *TAG_SYS  = "INFO_ESP";
static const char *TAG_GPIO = "GPIO";
static const char *TAG_TIMER = "TIMER_RT";
static const char *TAG_PWM = "PWM";

// Filas
static QueueHandle_t gpio_evt_queue = NULL;
static QueueHandle_t timer_evt_queue = NULL;
static QueueHandle_t pwm_evt_queue = NULL;

// Semáforo
static SemaphoreHandle_t semaphore_pwm = NULL;


// ================== TIPOS ==================

typedef struct {
    uint64_t count;
    uint64_t alarm;
} timer_event_t;

typedef struct {
    uint32_t hora;
    uint32_t minuto;
    uint32_t segundo;
} relogio_t;

typedef struct {
    bool automatico;
    int16_t incremento;
} PWM_elements_t;

// =================== ISR ===================

// Avalia se uma tarefa de maior prioridade foi desbloqueada e, se sim, solicita a troca de contexto imediata.

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR();
    }
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

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->alarm_value + 100000
    };

    gptimer_set_alarm_action(timer, &alarm_config);

    return (high_task_awoken == pdTRUE);
}

// ========== TASK LED (antigo) ===========
/*
// LED
static void led_control(void* arg)
{

    gpio_config_t io_conf;

    // BOTÕES
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io_conf);

    // LED
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    uint32_t io_num;
    int led_state = 0;

    while (1)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            int level = gpio_get_level(io_num);

            ESP_LOGI(TAG_GPIO, "GPIO[%" PRIu32 "] pressionado | Nivel: %d", io_num, level);

            // ===== CONTROLE DO LED =====

            if (io_num == B0)
            {
                gpio_set_level(LED, 1);
                led_state = 1;
                ESP_LOGI(TAG_GPIO, "LED LIGADO (Botao 0)");
            }
            else if (io_num == B1)
            {
                gpio_set_level(LED, 0);
                led_state = 0;
                ESP_LOGI(TAG_GPIO, "LED DESLIGADO (Botao 1)");
            }
            else if (io_num == B2)
            {
                led_state = !led_state;
                gpio_set_level(LED, led_state);

                ESP_LOGW(TAG_GPIO,
                "Atenção!!! Lógica do Botão 2 não possui debounce, sujeito a múltiplos acionamentos");

                if(led_state == 1)
                    ESP_LOGI(TAG_GPIO, "LED LIGADO (Botão 2)");
                
                else
                    ESP_LOGI(TAG_GPIO, "LED DESLIGADO (Botão 2)");
            }
        }
    }
}*/

// =================== TASK GPIO ===================

static void gpio_task(void* arg)
{
    gpio_config_t io_conf;

    // BOTÕES
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io_conf);

    // LED ANTIGO (mantido, mas não utilizado)
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    uint32_t io_num;
    PWM_elements_t msg;

    while (1)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            ESP_LOGI(TAG_GPIO, "Botao pressionado: %" PRIu32, io_num);

            // ===== NOVA LÓGICA PWM =====

            if (io_num == B0)
            {
                msg.automatico = true;
                msg.incremento = 0;
                ESP_LOGI(TAG_GPIO, "Modo AUTOMATICO");
            }
            else if (io_num == B1)
            {
                msg.automatico = false;
                msg.incremento = 0;
                ESP_LOGI(TAG_GPIO, "Modo MANUAL");
            }
            else if (io_num == B2)
            {
                msg.incremento = 300;
                ESP_LOGI(TAG_GPIO, "Incremento PWM");
            }

            xQueueSend(pwm_evt_queue, &msg, portMAX_DELAY);

            /*
            ===== FUNCIONALIDADE ANTIGA (DESATIVADA) =====

            if (io_num == B0)
                gpio_set_level(LED, 1);

            else if (io_num == B1)
                gpio_set_level(LED, 0);

            else if (io_num == B2)
                gpio_set_level(LED, !gpio_get_level(LED));
            */
        }
    }
}

// =================== TASK TIMER ===================

void timer_task(void *arg)
{
    timer_event_t evt;
    relogio_t relogio = {0,0,0};
    uint32_t contador_100ms = 0;

    gptimer_handle_t timer = NULL;

    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };

    gptimer_new_timer(&config, &timer);

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_callback,
    };

    gptimer_register_event_callbacks(timer, &cbs, timer_evt_queue);
    gptimer_enable(timer);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 100000
    };

    gptimer_set_alarm_action(timer, &alarm_config);
    gptimer_start(timer);

    while (1)
    {
        if (xQueueReceive(timer_evt_queue, &evt, portMAX_DELAY))
        {
            // 🔹 sincroniza PWM
            xSemaphoreGive(semaphore_pwm);

            // 🔹 relógio
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
                    relogio.hora = 0;

                ESP_LOGI(TAG_TIMER,
                    "Hora: %02lu:%02lu:%02lu",
                    relogio.hora,
                    relogio.minuto,
                    relogio.segundo);
            }
        }
    }
}

// =================== TASK PWM ===================

void pwm_task(void *arg)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch0 = {
        .channel = LEDC_CHANNEL_0,
        .gpio_num = PWM_LED_GPIO,
        .duty = 0,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0
    };

    ledc_channel_config_t ch1 = {
        .channel = LEDC_CHANNEL_1,
        .gpio_num = PWM_SCOPE_GPIO,
        .duty = 0,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0
    };

    ledc_channel_config(&ch0);
    ledc_channel_config(&ch1);

    int duty = 0;
    bool automatico = true;
    PWM_elements_t msg;

    while (1)
    {
        xSemaphoreTake(semaphore_pwm, portMAX_DELAY);

        if (xQueueReceive(pwm_evt_queue, &msg, 0))
        {
            if (msg.incremento == 0)
                automatico = msg.automatico;
            else if (!automatico)
                duty += msg.incremento;
        }

        if (automatico)
            duty += 200;

        if (duty > 8191)
            duty = 0;

        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, duty);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);

        ESP_LOGI(TAG_PWM, "Duty: %d | %s",
                 duty, automatico ? "AUTO" : "MANUAL");
    }
}

// =================== MAIN ===================

void app_main(void)
{
    // =================== INFO DO SISTEMA ===================

    ESP_LOGI(TAG_SYS, "Iniciando programa...");

    esp_chip_info_t chip_info;
    uint32_t flash_size;

    esp_chip_info(&chip_info);

    ESP_LOGI(TAG_SYS, "Chip: %s", CONFIG_IDF_TARGET);

    unsigned major = chip_info.revision / 100;
    unsigned minor = chip_info.revision % 100;

    ESP_LOGI(TAG_SYS, "Revisao: v%d.%d", major, minor);
    ESP_LOGI(TAG_SYS, "Nucleos: %d", chip_info.cores);

    ESP_LOGI(TAG_SYS, "WiFi: %s",
        (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "SIM" : "NAO");

    ESP_LOGI(TAG_SYS, "Bluetooth: %s",
        (chip_info.features & CHIP_FEATURE_BT) ? "SIM" : "NAO");

    ESP_LOGI(TAG_SYS, "BLE: %s",
        (chip_info.features & CHIP_FEATURE_BLE) ? "SIM" : "NAO");

    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK)
    {
        ESP_LOGI(TAG_SYS, "Flash: %" PRIu32 " MB",
                 flash_size / (1024 * 1024));
    }

    ESP_LOGI(TAG_SYS, "Heap minimo: %" PRIu32 " bytes",
             esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG_SYS, "ESP-IDF: %s", esp_get_idf_version());

    // Filas
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    timer_evt_queue = xQueueCreate(10, sizeof(timer_event_t));
    pwm_evt_queue = xQueueCreate(10, sizeof(PWM_elements_t));

    // Semáforo
    semaphore_pwm = xSemaphoreCreateBinary();

    // Tasks
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);
    xTaskCreate(timer_task, "timer_task", 4096, NULL, 5, NULL);
    xTaskCreate(pwm_task, "pwm_task", 4096, NULL, 6, NULL);

    // ISR
    gpio_install_isr_service(0);
    gpio_isr_handler_add(B0, gpio_isr_handler, (void*) B0);
    gpio_isr_handler_add(B1, gpio_isr_handler, (void*) B1);
    gpio_isr_handler_add(B2, gpio_isr_handler, (void*) B2);
}