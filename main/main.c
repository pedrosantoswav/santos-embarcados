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

static const char *TAG_MQTT = "mqtt_example";

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG_MQTT, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "setLED_value", 0);
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG_MQTT, "sent unsubscribe successful, msg_id=%d", msg_id);
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
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
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
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// ================== DISPLAY ===================

static const char *TAG = "example";

#define I2C_BUS_PORT  0

// Display Spec

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ    (400 * 1000)
#define EXAMPLE_PIN_NUM_SDA           19
#define EXAMPLE_PIN_NUM_SCL           18
#define EXAMPLE_PIN_NUM_RST           -1
#define EXAMPLE_I2C_HW_ADDR           0x3C

// The pixel number in horizontal and vertical

#define EXAMPLE_LCD_H_RES              128
#define EXAMPLE_LCD_V_RES              CONFIG_EXAMPLE_SSD1306_HEIGHT

// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8

#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2
#define EXAMPLE_LVGL_PALETTE_SIZE      8
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ

// To use LV_COLOR_FORMAT_I1, we need an extra buffer to hold the converted data
static uint8_t oled_buffer[EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 8];
// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;

static lv_obj_t *label_title;
static lv_obj_t *label_voltage;
static lv_obj_t *label_time;

void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *screen = lv_display_get_screen_active(disp);

    label_title = lv_label_create(screen);
    label_voltage = lv_label_create(screen);
    label_time = lv_label_create(screen);

    lv_label_set_text(label_title, "P6 - Pedro Santos");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 0);

    lv_label_set_text(label_voltage, "0 mV");
    lv_obj_align(label_voltage, LV_ALIGN_CENTER, 0, 0);

    lv_label_set_text(label_time, "00:00:00");
    lv_obj_align(label_time, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t io_panel, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);

    // This is necessary because LVGL reserves 2 x 4 bytes in the buffer, as these are assumed to be used as a palette. Skip the palette here
    // More information about the monochrome, please refer to https://docs.lvgl.io/9.2/porting/display.html#monochrome-displays
    px_map += EXAMPLE_LVGL_PALETTE_SIZE;

    uint16_t hor_res = lv_display_get_physical_horizontal_resolution(disp);
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;

    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            /* The order of bits is MSB first
                        MSB           LSB
               bits      7 6 5 4 3 2 1 0
               pixels    0 1 2 3 4 5 6 7
                        Left         Right
            */
            bool chroma_color = (px_map[(hor_res >> 3) * y  + (x >> 3)] & 1 << (7 - x % 8));

            /* Write to the buffer as required for the display.
            * It writes only 1-bit for monochrome displays mapped vertically.*/
            uint8_t *buf = oled_buffer + hor_res * (y >> 3) + (x);
            if (chroma_color) {
                (*buf) &= ~(1 << (y % 8));
            } else {
                (*buf) |= (1 << (y % 8));
            }
        }
    }
    // pass the draw buffer to the driver
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, oled_buffer);
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, EXAMPLE_LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, EXAMPLE_LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

// ================= DEFINIÇÕES =================

#define B0 21
#define B1 22
#define B2 23
#define B3 26
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
static const char *TAG_LCD   = "LCD";

// ================= FILAS =================

static QueueHandle_t gpio_evt_queue = NULL;
static QueueHandle_t timer_evt_queue = NULL;
static QueueHandle_t pwm_evt_queue = NULL;
static QueueHandle_t adc_queue = NULL;
static QueueHandle_t lcd_queue = NULL;

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

                    lcd_data.relogio = relogio;
                    lcd_data.voltage = adc_data.voltage;

                    xQueueSend(lcd_queue, &lcd_data, 0);
                }
            }
        }
    }
}

// ================= TASK DISPLAY ===============

void display_task(void *arg)
{

    ESP_LOGI(TAG, "Initialize I2C bus");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = EXAMPLE_PIN_NUM_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = EXAMPLE_I2C_HW_ADDR,
        .scl_speed_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .control_phase_bytes = 1,               // According to SSD1306 datasheet
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,   // According to SSD1306 datasheet
        .lcd_param_bits = EXAMPLE_LCD_CMD_BITS, // According to SSD1306 datasheet
        .dc_bit_offset = 6,                     // According to SSD1306 datasheet

    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SSD1306 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
    };

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = EXAMPLE_LCD_V_RES,
    };
    panel_config.vendor_config = &ssd1306_config;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Initialize LVGL");
    lv_init();
    // create a lvgl display
    lv_display_t *display = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    // associate the i2c panel handle to the display
    lv_display_set_user_data(display, panel_handle);
    // create draw buffer
    void *buf = NULL;
    ESP_LOGI(TAG, "Allocate separate LVGL draw buffers");
    // LVGL reserves 2 x 4 bytes in the buffer, as these are assumed to be used as a palette.
    size_t draw_buffer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 8 + EXAMPLE_LVGL_PALETTE_SIZE;
    buf = heap_caps_calloc(1, draw_buffer_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buf);

    // LVGL9 suooprt new monochromatic format.
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_FULL);
    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, example_lvgl_flush_cb);

    ESP_LOGI(TAG, "Register io panel event callback for LVGL flush ready notification");
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = example_notify_lvgl_flush_ready,
    };
    /* Register done callback */
    esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display);

    ESP_LOGI(TAG, "Use esp_timer as LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Display LVGL Scroll Text");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    _lock_acquire(&lvgl_api_lock);
    example_lvgl_demo_ui(display);
    _lock_release(&lvgl_api_lock);

   lcd_data_t lcd_data;

    while (1)
    {
        if (xQueueReceive(lcd_queue, &lcd_data, portMAX_DELAY))
        {
            char str_hora[16];
            char str_tensao[32];

            snprintf(str_hora,
                    sizeof(str_hora),
                    "%02lu:%02lu:%02lu",
                    lcd_data.relogio.hora,
                    lcd_data.relogio.minuto,
                    lcd_data.relogio.segundo);

            snprintf(str_tensao,
                    sizeof(str_tensao),
                    "%d mV",
                    lcd_data.voltage);

            _lock_acquire(&lvgl_api_lock);

            lv_label_set_text(label_time, str_hora);
            lv_label_set_text(label_voltage, str_tensao);

            _lock_release(&lvgl_api_lock);
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

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

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

    mqtt_app_start();

    //Filas

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    timer_evt_queue = xQueueCreate(10, sizeof(timer_event_t));
    pwm_evt_queue = xQueueCreate(10, sizeof(PWM_elements_t));
    adc_queue = xQueueCreate(10, sizeof(adc_data_t));
    lcd_queue = xQueueCreate(5, sizeof(lcd_data_t));

    //Semáforos

    semaphore_pwm = xSemaphoreCreateBinary();
    semaphore_adc = xSemaphoreCreateBinary();

    //Tasks

    xTaskCreate(gpio_task, "gpio", 2048, NULL, 10, NULL);
    xTaskCreate(timer_task, "timer", 4096, NULL, 5, NULL);
    xTaskCreate(pwm_task, "pwm", 4096, NULL, 6, NULL);
    xTaskCreate(adc_task, "adc", 4096, NULL, 6, NULL);
    xTaskCreate(display_task, "display", 4096, NULL, 4, NULL);

    //ISR

    gpio_install_isr_service(0);
    gpio_isr_handler_add(B0, gpio_isr_handler, (void*) B0);
    gpio_isr_handler_add(B1, gpio_isr_handler, (void*) B1);
    gpio_isr_handler_add(B2, gpio_isr_handler, (void*) B2);
}