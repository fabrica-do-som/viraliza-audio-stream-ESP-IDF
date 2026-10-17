#include "http_stream.h"

// [http_server] ---> http_stream ---> aac decoder ---> i2s_stream ---> DAC

static const char *TAG = "HTTP_STREAM";

// Event handler para capturar headers do servidor e eventos de ciclo de vida da conexão
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "Conexao TCP/TLS estabelecida com o servidor de streaming.");
            break;
        case HTTP_EVENT_ON_HEADER:
            // Intercepta metadados úteis comuns em servidores Icecast/Shoutcast
            if (strncasecmp(evt->header_key, "icy-", 4) == 0 || 
                strcasecmp(evt->header_key, "content-type") == 0) {
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

    if (stream_cfg == NULL || stream_cfg->url == NULL || stream_cfg->ring_buffer == NULL) {
        ESP_LOGE(TAG, "Parâmetros de inicialização inválidos.");
        vTaskDelete(NULL);
        return;
    }

    // Buffer de recepção intermediário na SRAM
    char rx_buffer[HTTP_CHUNK_SIZE];

    // Configuração do cliente baseada no exemplo da Espressif
    esp_http_client_config_t config = {
        .url = stream_cfg->url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .use_global_ca_store = false,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Falha ao inicializar esp_http_client");
        vTaskDelete(NULL);
        return;
    }

    // Headers para evitar injeção de texto nos pacotes de áudio (Shoutcast/Icecast)
    esp_http_client_set_header(client, "User-Agent", "ESP32-WebRadio/1.0");
    esp_http_client_set_header(client, "Accept", "audio/aacp, audio/aac, audio/*;q=0.9, */*;q=0.8");
    esp_http_client_set_header(client, "Icy-MetaData", "0");

    while (1) {
        ESP_LOGI(TAG, "Conectando a: %s", stream_cfg->url);

        // 1. Abre a conexão TCP/TLS (Low-level API)
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao abrir conexão HTTP (%s). Nova tentativa em 3s...", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        // 2. Busca e valida os cabeçalhos de resposta
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status = %d, Content-Length = %d", status_code, content_length);

        if (status_code >= 200 && status_code < 300) {
            // 3. Loop contínuo de leitura do fluxo de áudio
            while (1) {
                int read_len = esp_http_client_read(client, rx_buffer, sizeof(rx_buffer));

                if (read_len > 0) {
                    // Envia os bytes brutos para o RingBuffer (produtor para o Core 1)
                    BaseType_t ret = xRingbufferSend(
                        stream_cfg->ring_buffer,
                        rx_buffer,
                        read_len,
                        pdMS_TO_TICKS(200) // Timeout para não prender a task indefinidamente se o buffer encher
                    );

                    if (ret != pdTRUE) {
                        ESP_LOGW(TAG, "Buffer cheio! Core 1 ainda não consumiu os dados.");
                    }
                } else if (read_len == 0) {
                    // Servidor não enviou dados no instante ou fechou a conexão
                    if (!esp_http_client_is_chunked_response(client)) {
                        ESP_LOGW(TAG, "Conexão encerrada pelo servidor.");
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                } else {
                    ESP_LOGE(TAG, "Erro de leitura no socket (read_len = %d).", read_len);
                    break;
                }
            }
        } else if (status_code == 301 || status_code == 302) {
            ESP_LOGW(TAG, "Redirecionamento detectado (%d). Reconfigure com a URL final.", status_code);
        } else {
            ESP_LOGE(TAG, "Erro no servidor HTTP. Código retornado: %d", status_code);
        }

        // Fecha a conexão atual antes de reiniciar o loop de reconexão
        esp_http_client_close(client);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}