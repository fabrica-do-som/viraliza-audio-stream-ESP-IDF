#pragma once

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

/**
 * @brief Event group for WiFi events
 */
#define WIFI_STA_IPV4_OBTAINED_BIT  BIT0
#define WIFI_STA_IPV6_OBTAINED_BIT  BIT1
#define WIFI_FAIL_BIT               BIT2

/**
 * @brief Initialize WiFi in station (STA) mode
 * 
 * Set up the WiFi interface and connect to a WiFi network.
 * 
 * Event group wait for a connection an IP adrress assignment.
 * 
 * !! Must call esp_netif_init()
 * before calling this function.
 * 
 * @param[in] event_group Event group handle for WiFi and IP events. Pass NULL
 *                        to use the existing event group
 * 
 * @return
 * - ESP_OK -> Sucess
 * - Other erros -> Failure. (esp_err for error codes) 
 */
esp_err_t wifi_sta_init(void);

/**
 * @brief Initialize WiFi in station (STA) in UTFPR Network
 */
esp_err_t wifi_sta_utfpr_init(void);

/**
  * @brief Disable WiFi
  * 
  * @return
  * - ESP_OK -> Sucess
  * - Other erros -> Failure. (esp_err for error codes) 
  */
esp_err_t wifi_sta_stop(void);

/**
 * @brief Attempt to reconnect WiFi
 * 
 * @return
 * - ESP_OK -> Sucess
 * - Other erros -> Failure. (esp_err for error codes) 
 */
esp_err_t wifi_sta_reconnect(void);