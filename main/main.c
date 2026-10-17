#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_sta.h"
#include "http_stream.h"

static const char *TAG = "MAIN_TASK";

#define RADIO_URL "https://audio01.viaflux.com:7444/radio.aac"

static RingbufHandle_t aac_ring_buffer = NULL;
static http_stream_config_t stream_config;

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //esp_log_level_set("*", ESP_LOG_VERBOSE);
    //esp_log_level_set("wifi", ESP_LOG_WARN);

    // Inicializa e conecta o Wi-Fi
    ESP_LOGI(TAG, "Iniciando Wi-Fi...");
    ret = wifi_sta_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi Conectado!");
    } else {
        ESP_LOGE(TAG, "Falha na inicialização do Wi-Fi.");
        return;
    }

    // Aloca o RingBuffer compartilhado (64 KB)
    aac_ring_buffer = xRingbufferCreate(64 * 1024, RINGBUF_TYPE_BYTEBUF);
    if (aac_ring_buffer == NULL) {
        ESP_LOGE(TAG, "Falha ao alocar RingBuffer na memoria!");
        return;
    }

    // Configura os parâmetros do stream
    stream_config.url = RADIO_URL;
    stream_config.ring_buffer = aac_ring_buffer;

    xTaskCreatePinnedToCore(http_stream_task, "http_stream_task", 8192, (void *)&stream_config, 5, NULL, 0);

    //#TODO - DECODER I2S
}
