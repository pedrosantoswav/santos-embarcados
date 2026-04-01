/*
 * =========================================================
 * Projeto 1
 * Sistemas Embarcados
 * 
 * Autor: Pedro Henrique Silva dos Santos
 * Data: 01/04/2026
 * Versão: 0.2.3
 *
 * Descrição:
 * Programa que exibe informações do ESP32 utilizando ESP_LOGI
 * sem gerar reset do processador.
 * =========================================================
 */

#include <stdio.h>              // Biblioteca padrão de entrada/saída
#include <string.h>             // Suporte às Strings
#include <stdlib.h>             // Alocacação de Memória
#include <inttypes.h>           // Definições de tipos inteiros com tamanho fixo

#include "sdkconfig.h"          // Configurações do projeto ESP-IDF
#include "freertos/FreeRTOS.h"  // Kernel do FreeRTOS
#include "freertos/task.h"      // Manipulação de tarefas
#include "freertos/queue.h"     // Filas
#include "driver/gpio.h"

#include "esp_chip_info.h"      // Informações sobre o chip ESP
#include "esp_flash.h"          // Funções relacionadas à memória flash
#include "esp_system.h"         // Funções gerais do sistema
#include "esp_log.h"            // Sistema de logging (ESP_LOGI, ESP_LOGE, etc.)
#include "esp_idf_version.h"    // Versão do ESP-IDF

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

// Fila
static QueueHandle_t gpio_evt_queue = NULL;

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

// =================== TASK ===================

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

    // =================== CONFIGURA GPIO ===================

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

    // =================== FILA ===================

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // =================== TASK ===================

    // Criação da tarefa de controle do LED
    xTaskCreate(led_control, "led_control", 2048, NULL, 10, NULL);

    // =================== ISR ===================

    // Inicialização do serviço de interrupção
    gpio_install_isr_service(0);

    // Associação dos pinos à interrupção
    gpio_isr_handler_add(B0, gpio_isr_handler, (void*) B0);
    gpio_isr_handler_add(B1, gpio_isr_handler, (void*) B1);
    gpio_isr_handler_add(B2, gpio_isr_handler, (void*) B2);

    ESP_LOGI(TAG_GPIO, "Sistema pronto. Aguardando entrada...");
}