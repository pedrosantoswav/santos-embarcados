/*
 * =========================================================
 * Projeto 5
 * Sistemas Embarcados
 * 
 * Autor: Pedro Henrique Silva dos Santos
 * Data: 29/04/2026
 * Versão: 0.5.0
 *
 * Features:
 * - Informações do sistema
 * - Relógio digital
 * - PWM automático/manual
 * - ADC lendo PWM filtrado (RC)
 * 
 * =========================================================
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_idf_version.h"

// ADC
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// ================= DEFINIÇÕES =================

#define B0 21
#define B1 22
#define B2 23
#define GPIO_INPUT_PIN_SEL ((1ULL<<B0) | (1ULL<<B1) | (1ULL<<B2))

#define PWM_LED_GPIO   16
#define PWM_SCOPE_GPIO 33

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

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_3,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &cali) == ESP_OK)
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
    ESP_LOGI(TAG_SYS, "Inicializando...");

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    timer_evt_queue = xQueueCreate(10, sizeof(timer_event_t));
    pwm_evt_queue = xQueueCreate(10, sizeof(PWM_elements_t));
    adc_queue = xQueueCreate(10, sizeof(adc_data_t));

    semaphore_pwm = xSemaphoreCreateBinary();
    semaphore_adc = xSemaphoreCreateBinary();

    xTaskCreate(gpio_task, "gpio", 2048, NULL, 10, NULL);
    xTaskCreate(timer_task, "timer", 4096, NULL, 5, NULL);
    xTaskCreate(pwm_task, "pwm", 4096, NULL, 6, NULL);
    xTaskCreate(adc_task, "adc", 4096, NULL, 6, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(B0, gpio_isr_handler, (void*) B0);
    gpio_isr_handler_add(B1, gpio_isr_handler, (void*) B1);
    gpio_isr_handler_add(B2, gpio_isr_handler, (void*) B2);
}

/*

Codigo ADC

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gptimer.h"
#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// ================= TAGS =================
static const char *TAG_ADC   = "ADC_TASK";
static const char *TAG_TIMER = "TIMER";

// ================= FILAS =================
static QueueHandle_t adc_queue = NULL;
static QueueHandle_t timer_evt_queue = NULL;

// ================= SEMÁFORO =================
static SemaphoreHandle_t semaphore_adc = NULL;

// ================= TIPOS =================

typedef struct {
    int raw;
    int voltage;
} adc_data_t;

typedef struct {
    uint64_t count;
    uint64_t alarm;
} timer_event_t;

// ================= CALLBACK TIMER =================

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
        .alarm_count = edata->alarm_value + 100000 // 100 ms
    };

    gptimer_set_alarm_action(timer, &alarm_config);

    return (high_task_awoken == pdTRUE);
}

// ================= TASK ADC =================

void adc_task(void *arg)
{
    adc_oneshot_unit_handle_t adc_handle;

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_config, &adc_handle);

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT, // resolução máxima
    };

    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_3, &config);

    // calibração
    adc_cali_handle_t cali_handle = NULL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_3,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK)
        calibrated = true;
#endif

    int raw;
    int voltage;
    adc_data_t data;

    while (1)
    {
        // sincroniza com timer (100 ms)
        xSemaphoreTake(semaphore_adc, portMAX_DELAY);

        adc_oneshot_read(adc_handle, ADC_CHANNEL_3, &raw);

        if (calibrated)
            adc_cali_raw_to_voltage(cali_handle, raw, &voltage);
        else
            voltage = 0;

        data.raw = raw;
        data.voltage = voltage;

        // envia para timer
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

    timer_event_t evt;
    adc_data_t adc_data;

    int contador = 0;

    while (1)
    {
        if (xQueueReceive(timer_evt_queue, &evt, portMAX_DELAY))
        {
            // libera ADC a cada 100 ms
            xSemaphoreGive(semaphore_adc);

            contador++;

            // a cada 1 segundo (10 ciclos)
            if (contador == 10)
            {
                contador = 0;

                if (xQueueReceive(adc_queue, &adc_data, 0))
                {
                    ESP_LOGI(TAG_ADC,
                        "ADC Raw: %d | Voltage: %d mV",
                        adc_data.raw,
                        adc_data.voltage);
                }
            }
        }
    }
}

// ================= MAIN =================

void app_main(void)
{
    adc_queue = xQueueCreate(10, sizeof(adc_data_t));
    timer_evt_queue = xQueueCreate(10, sizeof(timer_event_t));

    semaphore_adc = xSemaphoreCreateBinary();

    xTaskCreate(adc_task, "adc_task", 4096, NULL, 5, NULL);
    xTaskCreate(timer_task, "timer_task", 4096, NULL, 6, NULL);
}

*/