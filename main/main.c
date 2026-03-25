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

#include <stdio.h>          // Biblioteca padrão de entrada/saída
#include <inttypes.h>      // Definições de tipos inteiros com tamanho fixo

#include "sdkconfig.h"     // Configurações do projeto ESP-IDF
#include "freertos/FreeRTOS.h" // Kernel do FreeRTOS
#include "freertos/task.h"     // Manipulação de tarefas

#include "esp_chip_info.h" // Informações sobre o chip ESP
#include "esp_flash.h"     // Funções relacionadas à memória flash
#include "esp_system.h"    // Funções gerais do sistema
#include "esp_log.h"       // Sistema de logging (ESP_LOGI, ESP_LOGE, etc.)
#include "esp_idf_version.h" // Versão do ESP-IDF

void app_main(void)
{
    // TAG usada para identificar as mensagens no log
    static const char *TAG = "INFO_ESP";

    // Mensagem inicial indicando que o programa começou
    ESP_LOGI(TAG, "Iniciando programa...");

    esp_chip_info_t chip_info; // Estrutura para armazenar informações do chip
    uint32_t flash_size;       // Variável para armazenar o tamanho da flash

    // Preenche a estrutura chip_info com dados do microcontrolador
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "Sobre o dispositivo:");

    // Exibe o modelo do chip (ex: esp32, esp32s3, etc.)
    ESP_LOGI(TAG, "Chip: %s", CONFIG_IDF_TARGET);

    // Calcula a versão do chip (major.minor)
    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;

    // Exibe a revisão do chip
    ESP_LOGI(TAG, "Revisão do chip: v%d.%d", major_rev, minor_rev);

    // Exibe o número de núcleos do processador
    ESP_LOGI(TAG, "Numero de nucleos: %d", chip_info.cores);

    ESP_LOGI(TAG, "Conectividade:");

    // Verifica se o chip possui suporte a Wi-Fi
    ESP_LOGI(TAG, "WiFi: %s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "SIM" : "NAO");

    // Verifica se possui Bluetooth clássico
    ESP_LOGI(TAG, "Bluetooth: %s",
             (chip_info.features & CHIP_FEATURE_BT) ? "SIM" : "NAO");

    // Verifica se possui BLE (Bluetooth Low Energy)
    ESP_LOGI(TAG, "BLE: %s",
             (chip_info.features & CHIP_FEATURE_BLE) ? "SIM" : "NAO");

    ESP_LOGI(TAG, "Armazenamento:");

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
}