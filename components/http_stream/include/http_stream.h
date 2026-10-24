#pragma once

#include "freertos/ringbuf.h"

#define HTTP_CHUNK_SIZE 1024

typedef struct
{
    const char *url; // URL da WebRadio (HTTP ou HTTPS)
    RingbufHandle_t ring_buffer; // Handle do RingBuffer onde os dados AAC serão escritos
} http_stream_config_t;

/**
 * @brief FreeRTOS Task - do continuous audio stream download
 *
 * @param pvParameters Ponteiro para a estrutura http_stream_config_t
 */
void http_stream_task(void *pvParameters);