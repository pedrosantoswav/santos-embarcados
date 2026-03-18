/*
 * =========================================================
 * Projeto 1
 * Sistemas Embarcados
 * 
 * Autor: Pedro Henrique Silva dos Santos
 * Data: 18/03/2026
 * Versão: 0.0.1
 *
 * Descrição:
 * Programa que exibe informações do ESP32 utilizando ESP_LOGI
 * sem gerar reset do processador.
 * =========================================================
 */

#include <stdio.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_idf_version.h"

void app_main(void)
{
    static const char *TAG = "INFO_ESP";

    ESP_LOGI(TAG, "Iniciando programa...\n");

    esp_chip_info_t chip_info;
    uint32_t flash_size;

    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "Sobre o dipositivo:\n");

    ESP_LOGI(TAG, "Chip: %s", CONFIG_IDF_TARGET);

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;

    ESP_LOGI(TAG, "Revisão do chip: v%d.%d", major_rev, minor_rev);
    ESP_LOGI(TAG, "Numero de nucleos: %d", chip_info.cores);

    ESP_LOGI(TAG, "\nConectividade:\n");

    ESP_LOGI(TAG, "WiFi: %s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "SIM" : "NAO");

    ESP_LOGI(TAG, "Bluetooth: %s",
             (chip_info.features & CHIP_FEATURE_BT) ? "SIM" : "NAO");

    ESP_LOGI(TAG, "BLE: %s",
             (chip_info.features & CHIP_FEATURE_BLE) ? "SIM" : "NAO");

    ESP_LOGI(TAG, "\nArmazenamento:\n");

    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK)
    {
        ESP_LOGI(TAG, "Flash: %" PRIu32 " MB (%s)",
                 flash_size / (1024 * 1024),
                 (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embutida" : "externa");
    }
    else
    {
        ESP_LOGE(TAG, "Erro ao obter tamanho da flash");
    }

    ESP_LOGI(TAG, "Heap minima livre: %" PRIu32 " bytes",
             esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG, "\nInformação de Software:\n");

    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());

    ESP_LOGI(TAG, "\nFim do Programa.");
}