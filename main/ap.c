#include "ap.h"
#include <stdio.h>
#include <string.h>

/* Ni 0 ni O, ni 1 ni I ni l : le mot de passe se lit et se recopie a la main. */
static const char ALPHABET[] = "23456789abcdefghjkmnpqrstuvwxyz";   /* 31 symboles */

void ap_password_from_random(const unsigned char rnd[AP_PASSWORD_LEN], char *out, size_t sz)
{
    size_t n = sizeof ALPHABET - 1;
    size_t i = 0;
    for (; i < AP_PASSWORD_LEN && i + 1 < sz; i++) out[i] = ALPHABET[rnd[i] % n];
    out[i] = 0;
}

bool ap_ssid_build(const unsigned char mac[6], const char *slug, char *out, size_t sz)
{
    int n = snprintf(out, sz, "OpenRFTest-%s-%02X%02X", slug, mac[4], mac[5]);
    return n > 0 && (size_t)n < sz;
}

#ifdef ESP_PLATFORM
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "ap";

static bool password_get_or_create(char *out, size_t sz)
{
    nvs_handle_t h;
    if (nvs_open("openrftest", NVS_READWRITE, &h) != ESP_OK) return false;
    size_t len = sz;
    if (nvs_get_str(h, "ap_pw", out, &len) == ESP_OK && strlen(out) == AP_PASSWORD_LEN) { nvs_close(h); return true; }
    unsigned char rnd[AP_PASSWORD_LEN];
    esp_fill_random(rnd, sizeof rnd);   /* apres esp_wifi_init : alea materiel */
    ap_password_from_random(rnd, out, sz);
    bool ok = nvs_set_str(h, "ap_pw", out) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool ap_start(const char *slug)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); err = nvs_flash_init(); }
    if (err != ESP_OK) return false;
    if (esp_netif_init() != ESP_OK) return false;
    if (esp_event_loop_create_default() != ESP_OK) return false;
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) return false;

    static char pw[AP_PASSWORD_LEN + 1], ssid[33];
    if (!password_get_or_create(pw, sizeof pw)) { ESP_LOGE(TAG, "pas de mot de passe : point d'acces NON leve"); return false; }
    unsigned char mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (!ap_ssid_build(mac, slug, ssid, sizeof ssid)) return false;

    wifi_config_t wc = {0};
    strncpy((char *)wc.ap.ssid, ssid, sizeof wc.ap.ssid - 1);
    wc.ap.ssid_len = strlen(ssid);
    strncpy((char *)wc.ap.password, pw, sizeof wc.ap.password - 1);
    wc.ap.max_connection = 4;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (esp_wifi_set_config(WIFI_IF_AP, &wc) != ESP_OK) return false;
    if (esp_wifi_start() != ESP_OK) return false;
    ESP_LOGW(TAG, "point d'acces : %s", ssid);
    ESP_LOGW(TAG, "mot de passe  : %s", pw);
    ESP_LOGW(TAG, "page          : http://192.168.4.1/");
    return true;
}
#endif
