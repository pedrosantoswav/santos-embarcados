/*
 * =========================================================
 * Projeto 1
 * Sistemas Embarcados
 * 
 * Autor: Pedro Henrique Silva dos Santos
 * Data: 18/03/2026
 * Versão: 0.1.1
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

#define GPIO_OUTPUT_IO_0    18
#define GPIO_OUTPUT_IO_1    19
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))

#define GPIO_INPUT_IO_0     4
#define GPIO_INPUT_IO_1     5
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0) | (1ULL<<GPIO_INPUT_IO_1))

#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
        }
    }
}

void app_main(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask =
            (1ULL << B0) |
            (1ULL << B1) |
            (1ULL << B2),

        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    gpio_config(&io_conf);

    // INICIALIZAÇÃO - INFORMAÇÕES DO DISPOSITIVO

    // TAG usada para identificar as mensagens no log
    static const char *TAG = "INFO_ESP";

    // Mensagem inicial indicando que o programa começou
    ESP_LOGI(TAG, "Iniciando programa...");

    esp_chip_info_t chip_info; // Estrutura para armazenar informações do chip
    uint32_t flash_size;       // Variável para armazenar o tamanho da flash

    // Preenche a estrutura chip_info com dados do microcontrolador
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "--- Sobre o dispositivo ---");

    // Exibe o modelo do chip (ex: esp32, esp32s3, etc.)
    ESP_LOGI(TAG, "Chip: %s", CONFIG_IDF_TARGET);

    // Calcula a versão do chip (major.minor)
    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;

    // Exibe a revisão do chip
    ESP_LOGI(TAG, "Revisão do chip: v%d.%d", major_rev, minor_rev);

    // Exibe o número de núcleos do processador
    ESP_LOGI(TAG, "Numero de nucleos: %d", chip_info.cores);

    ESP_LOGI(TAG, "--- Conectividade ---");

    // Verifica se o chip possui suporte a Wi-Fi
    ESP_LOGI(TAG, "WiFi: %s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "SIM" : "NAO");

    // Verifica se possui Bluetooth clássico
    ESP_LOGI(TAG, "Bluetooth: %s",
             (chip_info.features & CHIP_FEATURE_BT) ? "SIM" : "NAO");

    // Verifica se possui BLE (Bluetooth Low Energy)
    ESP_LOGI(TAG, "BLE: %s",
             (chip_info.features & CHIP_FEATURE_BLE) ? "SIM" : "NAO");

    ESP_LOGI(TAG, "--- Armazenamento ---");

    // Obtém o tamanho da memória flash
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK)
    {
        // Exibe o tamanho da flash em MB e se é embutida ou externa
        ESP_LOGI(TAG, "Flash: %" PRIu32 " MB (%s)",
                 flash_size / (1024 * 1024),
                 (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embutida" : "externa");
    }
    else
    {
        // Caso ocorra erro ao obter o tamanho da flash
        ESP_LOGE(TAG, "Erro ao obter tamanho da memória flash");
    }

    // Exibe a menor quantidade de heap livre registrada
    // (útil para análise de consumo de memória)
    ESP_LOGI(TAG, "Heap minima livre: %" PRIu32 " bytes",
             esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG, "Informação de Software:");

    // Exibe a versão do framework ESP-IDF em uso
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());

    // Mensagem final indicando término da execução
    ESP_LOGI(TAG, "Fim do Programa!!!");

    // Aguarda 1 segundo antes de finalizar a tarefa
    vTaskDelay(pdMS_TO_TICKS(1000));

    //------------------------------------------------------------------------------------------------------

    gpio_dump_io_configuration(stdout, (1ULL << 4) | (1ULL << 5) |(1ULL << 18) | (1ULL << 19));

    /*

    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    //change gpio interrupt type for one pin
    gpio_set_intr_type(GPIO_INPUT_IO_0, GPIO_INTR_ANYEDGE);

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
    xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_1, gpio_isr_handler, (void*) GPIO_INPUT_IO_1);

    //remove isr handler for gpio number.
    gpio_isr_handler_remove(GPIO_INPUT_IO_0);
    //hook isr handler for specific gpio pin again
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);

    printf("Minimum free heap size: %"PRIu32" bytes\n", esp_get_minimum_free_heap_size());

    int cnt = 0;
    while (1) {
        printf("cnt: %d\n", cnt++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_OUTPUT_IO_0, cnt % 2);
        gpio_set_level(GPIO_OUTPUT_IO_1, cnt % 2);
    }

    */
}