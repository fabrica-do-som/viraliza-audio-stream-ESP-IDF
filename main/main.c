#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_sta.h"
#include "http_stream.h"

static const char *TAG = "MAIN_TASK";

static RingbufHandle_t aac_ring_buffer = NULL;
static http_stream_config_t stream_config;

void app_main(void)
{
    setenv("TZ", "BRT3", 1);
    tzset();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializa e conecta o Wi-Fi
    ESP_LOGI(TAG, "Iniciando Wi-Fi...");
    if (wifi_sta_init() == ESP_OK)
    {
        ESP_LOGI(TAG, "Wi-Fi Conectado!");
    }
    else
    {
        ESP_LOGE(TAG, "Falha na inicialização do Wi-Fi.");
        return;
    }

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config)); // pre-configured NTP servers

    esp_err_t sync_err;

    // Aguarda a sincronização do relógio via SNTP (até 10 segundos)
    do
    {
        sync_err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));

        if (sync_err != ESP_OK)
        {
            ESP_LOGW("SNTP", "Aguardando horario: %s", esp_err_to_name(sync_err));
        }
    } while (sync_err != ESP_OK);
    ESP_LOGI("SNTP", "Relogio sincronizado");

    // Aloca o RingBuffer compartilhado (64 KB)
    aac_ring_buffer = xRingbufferCreate(64 * 1024, RINGBUF_TYPE_BYTEBUF);
    if (aac_ring_buffer == NULL)
    {
        ESP_LOGE(TAG, "Falha ao alocar RingBuffer na memoria!");
        return;
    }

    // Configura os parâmetros do stream
    stream_config.url = RADIO_URL;
    stream_config.ring_buffer = aac_ring_buffer;

    //xTaskCreatePinnedToCore(http_stream_task, "http_stream_task", 8192, (void *)&stream_config, 5, NULL, 0);

    // #TODO - DECODER I2S
}
