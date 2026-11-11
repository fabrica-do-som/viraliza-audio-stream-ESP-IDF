#include "http_stream.h"

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "freertos/FreeRTOS.h"

// [http_server] ---> http_stream ---> aac decoder ---> i2s_stream ---> DAC

static const char *TAG = "HTTP_STREAM";

// Event handler para capturar headers do servidor e eventos de ciclo de vida da conexão
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "Conexao TCP/TLS estabelecida com o servidor de streaming.");
        break;

    case HTTP_EVENT_ON_HEADER:
        // Intercepta metadados úteis comuns em servidores Icecast/Shoutcast
        if (strncasecmp(evt->header_key, "icy-", 4) == 0 || strcasecmp(evt->header_key, "content-type") == 0)
        {
            ESP_LOGI(TAG, "Header rádio: %s: %s", evt->header_key, evt->header_value);
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED");
        break;

    default:
        break;
    }
    return ESP_OK;
}

void http_stream_task(void *pvParameters)
{
    http_stream_config_t *stream_cfg = (http_stream_config_t *)pvParameters;
    ESP_LOGI(TAG, "Parâmetros recebidos: URL: %s / RingBuffer: %p", stream_cfg->url, (void *)stream_cfg->ring_buffer);

    if (stream_cfg == NULL || stream_cfg->url == NULL || stream_cfg->ring_buffer == NULL)
    {
        ESP_LOGE(TAG, "Parâmetros de inicialização inválidos.");
        vTaskDelete(NULL);
        return;
    }

    // Configuração do cliente HTTP para streaming de áudio
    esp_http_client_config_t config = {
        .url = stream_cfg->url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .use_global_ca_store = false,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    // Buffer de recepção intermediário na SRAM
    char rx_buffer[HTTP_CHUNK_SIZE];

    // Cria e inicializa o cliente HTTP
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "Falha ao inicializar esp_http_client");
        vTaskDelete(NULL);
        return;
    }

    // Headers para evitar injeção de texto nos pacotes de áudio (Shoutcast/Icecast)
    esp_http_client_set_header(client, "User-Agent", "ESP32-WebRadio/1.0");
    esp_http_client_set_header(client, "Accept", "audio/aacp, audio/aac, audio/*;q=0.9, */*;q=0.8");
    esp_http_client_set_header(client, "Icy-MetaData", "0");

    while (1)
    {
        ESP_LOGI(TAG, "Conectando a: %s", stream_cfg->url);

        // Abre a conexão HTTP com o servidor de streaming
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Falha ao abrir conexão HTTP (%s). Nova tentativa em 3s...", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        // 2. Busca e valida os cabeçalhos de resposta
        int64_t content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);

        if (content_length < 0)
        {
            ESP_LOGE(TAG, "Falha ao receber os cabecalhos: %" PRId64,
                     content_length);
        }
        else
        {
            ESP_LOGI(TAG, "HTTP Status = %d", status_code);
            ESP_LOGI(TAG, "Comprimento informado = %" PRId64, content_length);
            ESP_LOGI(TAG, "Resposta chunked = %s", esp_http_client_is_chunked_response(client) ? "sim" : "nao");
        }

        if (content_length >= 0 && status_code == 200)
        {
            size_t total_bytes = 0;

            int64_t inicio = esp_timer_get_time();
            int64_t ultimo_log = inicio;

            while (esp_timer_get_time() - inicio < 10000000LL)
            {
                int read_len = esp_http_client_read(
                    client,
                    rx_buffer,
                    sizeof(rx_buffer));

                if (read_len > 0)
                {
                    if (total_bytes == 0)
                    {
                        ESP_LOGI(TAG, "Amostra inicial do corpo HTTP:");

                        ESP_LOG_BUFFER_HEX_LEVEL(
                            TAG,
                            rx_buffer,
                            MIN(read_len, 32),
                            ESP_LOG_INFO);
                    }

                    total_bytes += (size_t)read_len;
                }
                else if (read_len == -ESP_ERR_HTTP_EAGAIN)
                {
                    ESP_LOGW(TAG, "Timeout aguardando dados");
                    break;
                }
                else if (read_len == 0)
                {
                    ESP_LOGW(TAG, "Fim da leitura do fluxo");
                    break;
                }
                else
                {
                    ESP_LOGE(TAG, "Erro de leitura: %d", read_len);
                    break;
                }

                int64_t agora = esp_timer_get_time();

                if (agora - ultimo_log >= 1000000LL)
                {
                    ESP_LOGI(TAG, "Total recebido: %zu bytes", total_bytes);
                    ultimo_log = agora;
                }
            }

            ESP_LOGI(TAG, "Teste encerrado: %zu bytes recebidos", total_bytes);
        }
        else if (status_code == 301 || status_code == 302)
        {
            ESP_LOGW(TAG, "Redirecionamento detectado (%d). Reconfigure com a URL final.", status_code);
        }
        else
        {
            ESP_LOGE(TAG, "Erro no servidor HTTP. Código retornado: %d", status_code);
        }

        // Fecha a conexão atual antes de reiniciar o loop de reconexão
        esp_http_client_close(client);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}