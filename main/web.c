/*
 * web : la page locale du firmware de test, servie sur son point d'acces.
 *
 * La page ne fait rien que la console ne fasse : les deux appellent ctrl.c.
 * Meme theme qu'OpenProfalux.
 */
#include "web.h"
#include "ctrl.h"
#include "ap.h"
#include "board_pins.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

extern const uint8_t rftest_html_start[] asm("_binary_rftest_html_start");
extern const uint8_t rftest_html_end[]   asm("_binary_rftest_html_end");

static esp_err_t h_page(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, (const char *)rftest_html_start, rftest_html_end - rftest_html_start - 1);
}

static esp_err_t h_state(httpd_req_t *r)
{
    static char buf[1024];
    size_t n = ctrl_state_json(buf, sizeof buf);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

/* Un JSON plat : {"k":"v","n":12}. On extrait une clef, chaine ou nombre. */
static bool field(const char *body, const char *key, char *out, size_t sz)
{
    char pat[48]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(body, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    bool q = *p == '"'; if (q) p++;
    size_t i = 0;
    while (*p && i + 1 < sz && (q ? *p != '"' : (*p != ',' && *p != '}' && *p != ' '))) out[i++] = *p++;
    out[i] = 0;
    return true;
}

static esp_err_t answer(httpd_req_t *r, const char *err)
{
    char buf[256];
    int n = snprintf(buf, sizeof buf, "{\"ok\":%s,\"message\":\"%s\"}", err ? "false" : "true", err ? err : "");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

/* POST /api/set : chaque clef presente est appliquee dans cet ordre ; le
   premier refus arrete et est renvoye. */
static esp_err_t h_set(httpd_req_t *r)
{
    char body[512] = {0}, v[32];
    int n = httpd_req_recv(r, body, sizeof body - 1);
    if (n <= 0) return answer(r, "corps vide");
    const char *e = NULL;
    if (!e && field(body, "radio", v, sizeof v)) e = ctrl_select(v);
    if (!e && field(body, "freq_khz", v, sizeof v)) e = ctrl_set_freq_hz((uint32_t)strtoul(v, NULL, 10) * 1000);
    if (!e && field(body, "chan", v, sizeof v)) e = ctrl_set_chan(v);
    if (!e && field(body, "mod", v, sizeof v)) e = ctrl_set_mod(v);
    if (!e && field(body, "baud", v, sizeof v)) e = ctrl_set_rate(strtoul(v, NULL, 10));
    if (!e && field(body, "dev_hz", v, sizeof v)) e = ctrl_set_dev(strtoul(v, NULL, 10));
    if (!e && field(body, "dbm", v, sizeof v)) e = ctrl_set_power(atoi(v));
    if (!e && field(body, "pa", v, sizeof v)) e = ctrl_set_pa(strtoul(v, NULL, 0));
    return answer(r, e);
}

static esp_err_t h_tx(httpd_req_t *r)
{
    char body[64] = {0}, v[8];
    if (httpd_req_recv(r, body, sizeof body - 1) <= 0 || !field(body, "on", v, sizeof v)) return answer(r, "on attendu");
    return answer(r, ctrl_tx(strcmp(v, "true") == 0));
}

static esp_err_t h_rx(httpd_req_t *r)
{
    char body[64] = {0}, v[8];
    if (httpd_req_recv(r, body, sizeof body - 1) <= 0 || !field(body, "on", v, sizeof v)) return answer(r, "on attendu");
    return answer(r, ctrl_rx(strcmp(v, "true") == 0));
}

/* POST /api/selftest : les verifications en JSON, [{radio,label,pass}], et le total. */
struct st_acc { char *buf; size_t sz, n; int checks; };
static void json_check(const char *radio, const char *label, bool pass, void *arg)
{
    struct st_acc *a = arg;
    a->n += snprintf(a->buf + a->n, a->sz > a->n ? a->sz - a->n : 0, "%s{\"radio\":\"%s\",\"label\":\"%s\",\"pass\":%s}",
                     a->checks ? "," : "", radio, label, pass ? "true" : "false");
    a->checks++;
}

static esp_err_t h_selftest(httpd_req_t *r)
{
    static char buf[1536];
    struct st_acc a = { buf, sizeof buf, 0, 0 };
    a.n = snprintf(buf, sizeof buf, "{\"checks\":[");
    int fail = ctrl_selftest(json_check, &a);
    a.n += snprintf(buf + a.n, sizeof buf > a.n ? sizeof buf - a.n : 0, "],\"total\":%d,\"fail\":%d,\"ok\":%s}", a.checks, fail, fail ? "false" : "true");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, a.n);
}

bool web_start(void)
{
    if (!ap_start(BOARD_AP_SLUG)) return false;
    httpd_handle_t s = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s, &cfg) != ESP_OK) return false;
    const httpd_uri_t routes[] = {
        { .uri = "/",          .method = HTTP_GET,  .handler = h_page },
        { .uri = "/api/state", .method = HTTP_GET,  .handler = h_state },
        { .uri = "/api/set",   .method = HTTP_POST, .handler = h_set },
        { .uri = "/api/tx",    .method = HTTP_POST, .handler = h_tx },
        { .uri = "/api/rx",    .method = HTTP_POST, .handler = h_rx },
        { .uri = "/api/selftest", .method = HTTP_POST, .handler = h_selftest },
    };
    for (unsigned i = 0; i < sizeof routes / sizeof routes[0]; i++) httpd_register_uri_handler(s, &routes[i]);
    return true;
}
