/*
 * =========================================================
 * Projeto 5
 * Sistemas Embarcados
 * 
 * Autor: Pedro Henrique Silva dos Santos
 * Data: 13/05/2026
 * Versão: 0.5.1
 *
 * Features:
 * - Informações detalhadas do sistema
 * - Relógio digital
 * - PWM automático/manual
 * - Leitura PWM através de ADC
 * - (desativado) Controle de LED da placa via botões
 * 
 * =========================================================
 */

#include <stdio.h>                  // Biblioteca padrão de entrada/saída
#include <string.h>                 // Suporte às Strings
#include <stdlib.h>                 // Alocacação de Memória
#include <inttypes.h>               // Definições de tipos inteiros com tamanho fixo
#include <stdbool.h>                // Suporte ao tipo booleano (true/false)

#include "sdkconfig.h"              // Configurações do projeto ESP-IDF

#include "freertos/FreeRTOS.h"      // Kernel do FreeRTOS
#include "freertos/task.h"          // Manipulação de tarefas
#include "freertos/queue.h"         // Filas
#include "freertos/semphr.h"        // Semáforos

#include "driver/gpio.h"            // Configurações pinos GPIO
#include "driver/gptimer.h"         // Configurações TIMER
#include "driver/ledc.h"            // Configurações PWM (LEDC)

#include "esp_chip_info.h"          // Informações sobre o chip ESP
#include "esp_flash.h"              // Funções relacionadas à memória flash
#include "esp_system.h"             // Funções gerais do sistema
#include "esp_log.h"                // Sistema de logging (ESP_LOGI, ESP_LOGE, etc.)
#include "esp_idf_version.h"        // Versão do ESP-IDF

#include "esp_adc/adc_oneshot.h"    // ADC no modo "oneshot" (leitura sob demanda)
#include "esp_adc/adc_cali.h"       // Calibração ADC
#include "esp_adc/adc_cali_scheme.h"// Implementação de calibração (curve e line fitting)

// ================= DEFINIÇÕES =================

#define B0 21
#define B1 22
#define B2 23
#define GPIO_INPUT_PIN_SEL ((1ULL<<B0) | (1ULL<<B1) | (1ULL<<B2))

#define PWM_LED_GPIO   16
#define PWM_SCOPE_GPIO 33

/*
// LED (antigo)
#define LED 2
#define GPIO_OUTPUT_PIN_SEL (1ULL<<LED)
*/

// ================= TAGs =================

static const char *TAG_SYS   = "SYS";
static const char *TAG_GPIO  = "GPIO";
static const char *TAG_TIMER = "TIMER";
static const char *TAG_PWM   = "PWM";
static const char *TAG_ADC   = "ADC";

// ================= FILAS =================

static QueueHandle_t gpio_evt_queue = NULL;
static QueueHandle_t timer_evt_queue = NULL;
static QueueHandle_t pwm_evt_queue = NULL;
static QueueHandle_t adc_queue = NULL;

// ================= SEMÁFOROS =================

static SemaphoreHandle_t semaphore_pwm = NULL;
static SemaphoreHandle_t semaphore_adc = NULL;

// ================= TIPOS =================

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

typedef struct {
    int raw;
    int voltage;
} adc_data_t;

// ================= ISR =================

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static bool IRAM_ATTR timer_callback(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *user_data)
{
    BaseType_t hp = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;

    timer_event_t evt = {
        .count = edata->count_value,
        .alarm = edata->alarm_value
    };

    xQueueSendFromISR(queue, &evt, &hp);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->alarm_value + 100000
    };
    gptimer_set_alarm_action(timer, &alarm_config);

    return hp == pdTRUE;
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

// ================= TASK GPIO =================

static void gpio_task(void* arg)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = GPIO_INPUT_PIN_SEL,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&io_conf);

    uint32_t io_num;
    PWM_elements_t msg;

    while (1)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            if (io_num == B0)
            {
                msg.automatico = true;
                msg.incremento = 0;
            }
            else if (io_num == B1)
            {
                msg.automatico = false;
                msg.incremento = 0;
            }
            else
            {
                msg.incremento = 300;
            }

            xQueueSend(pwm_evt_queue, &msg, portMAX_DELAY);
        }
    }
}

// ================= TASK PWM =================

void pwm_task(void *arg)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch0 = {
        .channel = LEDC_CHANNEL_0,
        .gpio_num = PWM_LED_GPIO,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0
    };

    ledc_channel_config_t ch1 = {
        .channel = LEDC_CHANNEL_1,
        .gpio_num = PWM_SCOPE_GPIO,
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

        if (automatico) duty += 200;
        if (duty > 8191) duty = 0;

        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, duty);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
    }
}


// ================= TASK ADC =================

void adc_task(void *arg)
{
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init = {.unit_id = ADC_UNIT_1};
    adc_oneshot_new_unit(&init, &adc_handle);

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_3, &config);

    adc_cali_handle_t cali = NULL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

    adc_cali_line_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1,                  // seleciona o ADC utilizado (ADC1)
        .atten = ADC_ATTEN_DB_12,               // define a atenuação do sinal (~0V a 3,3V default exemplo)
        .bitwidth = ADC_BITWIDTH_DEFAULT,       // resolução do adc (12 bits)
    };

    if (adc_cali_create_scheme_line_fitting(&cal, &cali) == ESP_OK)  // se o esp nn encontrar erro
        calibrated = true;

#endif

    adc_data_t data;

    while (1)
    {
        xSemaphoreTake(semaphore_adc, portMAX_DELAY);

        adc_oneshot_read(adc_handle, ADC_CHANNEL_3, &data.raw);

        if (calibrated)
            adc_cali_raw_to_voltage(cali, data.raw, &data.voltage);
        else
            data.voltage = 0;

        xQueueSend(adc_queue, &data, portMAX_DELAY);
    }
}

// ================= TASK TIMER =================

void timer_task(void *arg)
{
    gptimer_handle_t timer;
    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };

    gptimer_new_timer(&config, &timer);

    gptimer_event_callbacks_t cbs = {.on_alarm = timer_callback};
    gptimer_register_event_callbacks(timer, &cbs, timer_evt_queue);

    gptimer_enable(timer);

    gptimer_alarm_config_t alarm = {.alarm_count = 100000};
    gptimer_set_alarm_action(timer, &alarm);
    gptimer_start(timer);

    relogio_t relogio = {0};
    uint32_t count = 0;
    adc_data_t adc_data;
    timer_event_t evt;

    while (1)
    {
        if (xQueueReceive(timer_evt_queue, &evt, portMAX_DELAY))
        {
            xSemaphoreGive(semaphore_pwm);
            xSemaphoreGive(semaphore_adc);

            count++;

            if (count == 10)
            {
                count = 0;
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

                ESP_LOGI(TAG_TIMER, "Hora: %02lu:%02lu:%02lu",
                         relogio.hora, relogio.minuto, relogio.segundo);

                if (xQueueReceive(adc_queue, &adc_data, 0))
                {
                    ESP_LOGI(TAG_ADC,
                        "RAW: %d | Voltage: %d mV",
                        adc_data.raw, adc_data.voltage);
                }
            }
        }
    }
}

// ================= MAIN =================

void app_main(void)
{

    ESP_LOGI(TAG_SYS, "Iniciando...");

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

    //Filas

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    timer_evt_queue = xQueueCreate(10, sizeof(timer_event_t));
    pwm_evt_queue = xQueueCreate(10, sizeof(PWM_elements_t));
    adc_queue = xQueueCreate(10, sizeof(adc_data_t));

    //Semáforos

    semaphore_pwm = xSemaphoreCreateBinary();
    semaphore_adc = xSemaphoreCreateBinary();

    //Tasks

    xTaskCreate(gpio_task, "gpio", 2048, NULL, 10, NULL);
    xTaskCreate(timer_task, "timer", 4096, NULL, 5, NULL);
    xTaskCreate(pwm_task, "pwm", 4096, NULL, 6, NULL);
    xTaskCreate(adc_task, "adc", 4096, NULL, 6, NULL);

    //ISR

    gpio_install_isr_service(0);
    gpio_isr_handler_add(B0, gpio_isr_handler, (void*) B0);
    gpio_isr_handler_add(B1, gpio_isr_handler, (void*) B1);
    gpio_isr_handler_add(B2, gpio_isr_handler, (void*) B2);
}