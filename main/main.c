/*
 * =========================================================
 * Projeto 6
 * Sistemas Embarcados
 * 
 * Autor: Pedro Henrique Silva dos Santos
 * Data: 03/06/2026
 * Versão: 0.7.0
 *
 * Features:
 * - Informações detalhadas do sistema
 * - Relógio digital
 * - PWM automático/manual
 * - Leitura PWM através de ADC
 * - Display LCD com tensão e horário
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

//Bibliotecas do exemplo I2C_OLED

#include <sys/lock.h>
#include <sys/param.h>
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "lvgl.h"
#include <unistd.h> 

#include "esp_lcd_panel_vendor.h"

#include <stdint.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "mqtt_client.h"

// ================= DEFINIÇÕES =================

/*#define B0 21
#define B1 22
#define B2 23
#define B3 26
#define GPIO_INPUT_PIN_SEL ((1ULL<<B0) | (1ULL<<B1) | (1ULL<<B2))*/

#define PWM_LED_GPIO   16
#define PWM_SCOPE_GPIO 33

// ================= TAGs =================

static const char *TAG_SYS   = "SYS";
static const char *TAG_TIMER = "TIMER";
static const char *TAG_PWM   = "PWM";
static const char *TAG_ADC   = "ADC";

// ================= FILAS =================


static QueueHandle_t timer_evt_queue = NULL;
static QueueHandle_t pwm_evt_queue = NULL;
static QueueHandle_t adc_queue = NULL;
static QueueHandle_t mqtt_pwm_queue = NULL;


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

typedef struct {
    relogio_t relogio;
    int voltage;
} lcd_data_t;

static const char *TAG_MQTT = "mqtt_example";

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG_MQTT, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    int msg_id;

    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "setLED_value", 0);
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "setLED_value", "data", 0, 0, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
    {
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");

        char payload[16];

        memset(payload, 0, sizeof(payload));

        memcpy(payload,
            event->data,
            MIN(event->data_len, sizeof(payload)-1));

        int duty = atoi(payload);

        if(duty < 0)
            duty = 0;

        if(duty > 8191)
            duty = 8191;

        xQueueSend(mqtt_pwm_queue, &duty, 0);

        ESP_LOGI(TAG_MQTT, "PWM recebido via MQTT = %d", duty);

        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG_MQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {

        .broker.address.uri = "mqtt://g2device:g2device@node02.myqtthub.com:1883",
        // CONFIG_BROKER_URL: username, senha
        .credentials.client_id = "g2device",

    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// ================= ISR =================

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

// ================= TASK PWM =================

void pwm_task(void *arg)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch0 = {
        .channel = LEDC_CHANNEL_0,
        .gpio_num = PWM_LED_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0
    };

    ledc_channel_config_t ch1 = {
        .channel = LEDC_CHANNEL_1,
        .gpio_num = PWM_SCOPE_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0
    };

    ledc_channel_config(&ch0);
    ledc_channel_config(&ch1);

    int duty = 0;
    int mqtt_duty;

    bool automatico = false;
    PWM_elements_t msg;

   while (1)
    {
        xSemaphoreTake(semaphore_pwm, portMAX_DELAY);

        if(xQueueReceive(mqtt_pwm_queue, &mqtt_duty, 0))
        {
            duty = (mqtt_duty * 8191) / 100;
            
            ESP_LOGI(TAG_PWM,
                    "MQTT=%d%% Duty=%d",
                    mqtt_duty,
                    duty);
        }

        /*if (xQueueReceive(pwm_evt_queue, &msg, 0))
        {
            if (msg.incremento == 0)
                automatico = msg.automatico;
            else if (!automatico)
                duty += msg.incremento;
        }

        if (automatico)
            duty += 200;*/

       if (duty > 8191)
            duty = 0;

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
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

    adc_cali_line_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1,                  // seleciona o ADC utilizado (ADC1)
        .atten = ADC_ATTEN_DB_12,               // define a atenuação do sinal (~0V a 3,3V default exemplo)
        .bitwidth = ADC_BITWIDTH_DEFAULT,       // resolução do adc (12 bits)
    };

    if (adc_cali_create_scheme_line_fitting(&cal, &cali) == ESP_OK)  // se o esp nn encontrar erro
        calibrated = true;

    adc_data_t data;

    while (1)
    {
        xSemaphoreTake(semaphore_adc, portMAX_DELAY);

        adc_oneshot_read(adc_handle, ADC_CHANNEL_3, &data.raw);

        if (calibrated)
            adc_cali_raw_to_voltage(cali, data.raw, &data.voltage);
        else
            data.voltage = 0;

        xQueueOverwrite(adc_queue, &data);
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
    lcd_data_t lcd_data;

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

                char str_hora[9]; // "HH:MM:SS" + '\0'

                snprintf(str_hora,
                    sizeof(str_hora),
                    "%02lu:%02lu:%02lu",
                    relogio.hora,
                    relogio.minuto,
                    relogio.segundo);
                
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

    //MQTT
/*
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());*/

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());


    //Filas

    timer_evt_queue = xQueueCreate(10, sizeof(timer_event_t));
    pwm_evt_queue = xQueueCreate(10, sizeof(PWM_elements_t));
    adc_queue = xQueueCreate(1, sizeof(adc_data_t));
    mqtt_pwm_queue = xQueueCreate(5, sizeof(int));

     mqtt_app_start();
  
    //Semáforos

    semaphore_pwm = xSemaphoreCreateBinary();
    semaphore_adc = xSemaphoreCreateBinary();

    //Tasks

    xTaskCreate(timer_task, "timer", 4096, NULL, 5, NULL);
    xTaskCreate(pwm_task, "pwm", 4096, NULL, 6, NULL);
    xTaskCreate(adc_task, "adc", 4096, NULL, 6, NULL);

}