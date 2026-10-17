#include "wifi_sta.h"

static const char *TAG = "wifi_sta";

#define MAX_RETRY 5
static int retry_num = 0;

static EventGroupHandle_t wifi_event_group;
static esp_netif_t *sta_netif = NULL;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;

#define WIFI_SSID "Noctua"
#define WIFI_PASS "38497247"

// Event handler: WiFi events
static void on_wifi_event(void *handle_arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        // Chose event type
        switch (event_id)
        {
        // WiFi Started
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        // WiFi Stopped
        case WIFI_EVENT_STA_DISCONNECTED:
            if (retry_num < MAX_RETRY)
            {
                esp_wifi_connect();
                retry_num++;
                ESP_LOGI(TAG, "Tentando reconectar...");
            }
            else
            {
                ESP_LOGE(TAG, "Falha ao conectar após %d tentativas.", MAX_RETRY);
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            }
            ESP_LOGE(TAG, "Erro ao conectar ao AP");
            break;

        default:
            break;
        }
    }
}

// Recebe o endereço IP na interface WiFi pelo DHCP
static void on_ip_event(void *handle_arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void *event_data)
{
    if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t *event_ip = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "=== Endereço IPv4 obtido ===");
            ESP_LOGI(TAG, "- IPv4: " IPSTR, IP2STR(&event_ip->ip_info.ip));
            ESP_LOGI(TAG, "- Máscara de subrede: " IPSTR, IP2STR(&event_ip->ip_info.netmask));
            ESP_LOGI(TAG, "- Gateway: " IPSTR, IP2STR(&event_ip->ip_info.gw));
            retry_num = 0;
            xEventGroupSetBits(wifi_event_group, WIFI_STA_IPV4_OBTAINED_BIT);
            break;

        case IP_EVENT_GOT_IP6:
            ip_event_got_ip6_t *event_ip6 = (ip_event_got_ip6_t *)event_data;
            ESP_LOGI(TAG, "=== Endereço IPv6 obtido ===");
            ESP_LOGI(TAG, "- IPv6: " IPV6STR, IPV62STR(event_ip6->ip6_info.ip));
            retry_num = 0;
            xEventGroupSetBits(wifi_event_group, WIFI_STA_IPV6_OBTAINED_BIT);
            break;

        case IP_EVENT_STA_LOST_IP:
            xEventGroupClearBits(wifi_event_group, WIFI_STA_IPV4_OBTAINED_BIT);
            xEventGroupClearBits(wifi_event_group, WIFI_STA_IPV6_OBTAINED_BIT);
            ESP_LOGE(TAG, "WiFi perdeu o endereço de IP");
        default:
            break;
        }
    }
}

esp_err_t wifi_sta_init(void)
{
    retry_num = 0;

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &on_wifi_event,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &on_ip_event,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "",
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Inicialização do AP em STA concluída.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_STA_IPV4_OBTAINED_BIT | WIFI_STA_IPV6_OBTAINED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_STA_IPV4_OBTAINED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID: %s", WIFI_SSID);
        return ESP_OK;
    }
    else if (bits & WIFI_STA_IPV6_OBTAINED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID: %s", WIFI_SSID);
        return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", WIFI_SSID);
        return ESP_FAIL;
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t wifi_sta_stop(void)
{
    if (instance_any_id) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    }
    if (instance_got_ip) {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, instance_got_ip);
    }

    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        err = esp_wifi_deinit();
    }

    if (sta_netif) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }

    if (wifi_event_group != NULL) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }

    ESP_LOGI(TAG, "Wi-Fi STA parado.");
    return err;
}

esp_err_t wifi_sta_reconnect(void){
    retry_num = 0;
    if(wifi_event_group != NULL){
        xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
    }
    return esp_wifi_connect(); // OK -> ESP_OK / ERROR:
                               // - ESP_ERR_WIFI_NOT_INIT: WiFi was not initialized by esp_wifi_init
                               // - ESP_ERR_WIFI_NOT_STARTED: WiFi was not started by esp_wifi_start
                               // - ESP_FAIL: other WiFi internal errors
}