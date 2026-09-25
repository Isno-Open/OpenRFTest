#include "ctrl.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const board_radio_t RADIOS[BOARD_RADIO_COUNT] = BOARD_RADIOS_INIT;
static cc1101_state_t s_st[BOARD_RADIO_COUNT];
static int s_cur = 0;
static char s_msg[160];

/* Un mutex recursif serialise tous les reglages : la console et la page HTTP
 * appellent les memes ctrl_* depuis deux taches, sur le meme etat radio et le
 * meme SPI. Recursif car un reglage en appelle un autre (chan -> freq) et le
 * self-test enchaine tx/rx/reglages. Cree dans ctrl_init, avant toute tache. */
static SemaphoreHandle_t s_lock;
#define CTRL_LOCK()   xSemaphoreTakeRecursive(s_lock, portMAX_DELAY)
#define CTRL_UNLOCK() xSemaphoreGiveRecursive(s_lock)

static rf_band_t band_of(int r) { return (rf_band_t){ RADIOS[r].low_khz, RADIOS[r].high_khz, RADIOS[r].max_dbm }; }
static int band_mhz(int r) { return atoi(RADIOS[r].band); }
static void led(bool on) { if (BOARD_PIN_LED >= 0) gpio_set_level(BOARD_PIN_LED, on == (bool)BOARD_LED_ACTIVE_HIGH); }

static const char *refuse(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(s_msg, sizeof s_msg, fmt, ap);
    va_end(ap);
    return s_msg;
}

/* Un reglage change pendant que la radio est active est reapplique sur-le-champ. */
static const char *reapply(int r)
{
    if (!s_st[r].tx && !s_st[r].rx) return NULL;
    return cc1101_apply(r, &s_st[r]) ? NULL : refuse("registres impossibles a calculer pour ce reglage");
}

void ctrl_init(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (BOARD_PIN_LED >= 0) gpio_set_direction(BOARD_PIN_LED, GPIO_MODE_OUTPUT);
    led(false);
    cc1101_setup();
    for (int r = 0; r < BOARD_RADIO_COUNT; r++) {
        uint8_t pa = 0x00;
        rf_patable(band_mhz(r), 0, &pa);
        s_st[r] = (cc1101_state_t){ .freq_hz = (uint32_t)RADIOS[r].carrier_khz * 1000, .mod = RF_MOD_CW,
                                    .baud = 1200, .dev_hz = 5157, .dbm = 0, .pa = pa };
    }
}

int ctrl_radio_count(void) { return BOARD_RADIO_COUNT; }
const char *ctrl_band_name(int r) { return RADIOS[r].band; }
int ctrl_current(void) { return s_cur; }
const cc1101_state_t *ctrl_state(int r) { return &s_st[r]; }

static const char *ctrl_select_l(const char *band)
{
    for (int r = 0; r < BOARD_RADIO_COUNT; r++)
        if (strcmp(band, RADIOS[r].band) == 0) { s_cur = r; return NULL; }
    return refuse("radio inconnue : %s", band);
}

static const char *ctrl_set_freq_hz_l(uint32_t hz)
{
    rf_band_t b = band_of(s_cur);
    if (!rf_in_band(hz, &b))
        return refuse("REFUSE : %lu kHz hors de la plage declaree %d a %d kHz de la radio %s",
                      (unsigned long)(hz / 1000), b.low_khz, b.high_khz, RADIOS[s_cur].band);
    s_st[s_cur].freq_hz = hz;
    return reapply(s_cur);
}

static const char *ctrl_set_chan_l(const char *w)
{
    const board_radio_t *R = &RADIOS[s_cur];
    uint32_t khz = strcmp(w, "low") == 0 ? R->low_khz : strcmp(w, "high") == 0 ? R->high_khz
                 : strcmp(w, "mid") == 0 ? R->carrier_khz : 0;
    if (!khz) return refuse("chan low | mid | high");
    return ctrl_set_freq_hz(khz * 1000);
}

static const char *ctrl_set_mod_l(const char *n)
{
    if (strcmp(n, "cw") == 0) s_st[s_cur].mod = RF_MOD_CW;
    else if (strcmp(n, "ook") == 0) s_st[s_cur].mod = RF_MOD_OOK;
    else if (strcmp(n, "2fsk") == 0) s_st[s_cur].mod = RF_MOD_2FSK;
    else return refuse("modulation inconnue : %s (cw, ook, 2fsk)", n);
    return reapply(s_cur);
}

static const char *ctrl_set_rate_l(uint32_t baud)
{
    uint8_t e, m;
    if (!rf_datarate_regs(baud, &e, &m)) return refuse("REFUSE : debit %lu hors plage", (unsigned long)baud);
    s_st[s_cur].baud = baud;
    return reapply(s_cur);
}

static const char *ctrl_set_dev_l(uint32_t hz)
{
    uint8_t dv;
    if (!rf_deviation_reg(hz, &dv)) return refuse("REFUSE : deviation %lu Hz hors plage", (unsigned long)hz);
    s_st[s_cur].dev_hz = hz;
    return reapply(s_cur);
}

static const char *ctrl_set_power_l(int dbm)
{
    rf_band_t b = band_of(s_cur);
    if (!rf_power_allowed(dbm, &b)) return refuse("REFUSE : %d dBm au-dela des %d dBm declares", dbm, b.max_dbm);
    uint8_t pa;
    if (!rf_patable(band_mhz(s_cur), dbm, &pa))
        return refuse("REFUSE : %d dBm n'est pas dans la table ; donner une valeur PATABLE brute mesuree", dbm);
    s_st[s_cur].dbm = dbm; s_st[s_cur].pa = pa;
    return reapply(s_cur);
}

static const char *ctrl_set_pa_l(unsigned pa)
{
    if (pa > 0xFF) return refuse("PATABLE : un octet");
    s_st[s_cur].dbm = -128; s_st[s_cur].pa = (uint8_t)pa;
    return reapply(s_cur);
}

static const char *ctrl_tx_l(bool on)
{
    for (int r = 0; r < BOARD_RADIO_COUNT; r++) { s_st[r].tx = false; if (on) s_st[r].rx = false; }
    s_st[s_cur].tx = on;
    if (on) { if (!cc1101_apply(s_cur, &s_st[s_cur])) { s_st[s_cur].tx = false; return refuse("emission impossible"); } }
    else cc1101_idle(s_cur);
    led(on);
    return NULL;
}

static const char *ctrl_rx_l(bool on)
{
    s_st[s_cur].rx = on; s_st[s_cur].tx = false;
    if (on) { if (!cc1101_apply(s_cur, &s_st[s_cur])) { s_st[s_cur].rx = false; return refuse("ecoute impossible"); } }
    else cc1101_idle(s_cur);
    led(false);
    return NULL;
}

static size_t ctrl_state_json_l(char *out, size_t sz)
{
    static const char *M[] = { "cw", "ook", "2fsk" };
    size_t n = snprintf(out, sz, "{\"current\":\"%s\",\"radios\":[", RADIOS[s_cur].band);
    for (int r = 0; r < BOARD_RADIO_COUNT; r++) {
        uint8_t p, v;
        bool present = cc1101_present(r, &p, &v);
        const cc1101_state_t *s = &s_st[r];
        n += snprintf(out + n, sz > n ? sz - n : 0,
            "%s{\"band\":\"%s\",\"present\":%s,\"partnum\":%u,\"version\":%u,\"low_khz\":%d,\"high_khz\":%d,"
            "\"carrier_khz\":%d,\"max_dbm\":%d,\"freq_khz\":%lu,\"mod\":\"%s\",\"baud\":%lu,\"dev_hz\":%lu,"
            "\"dbm\":%d,\"pa\":%u,\"tx\":%s,\"rx\":%s,\"marc\":%u,\"rssi\":%d}",
            r ? "," : "", RADIOS[r].band, present ? "true" : "false", p, v,
            RADIOS[r].low_khz, RADIOS[r].high_khz, RADIOS[r].carrier_khz, RADIOS[r].max_dbm,
            (unsigned long)(s->freq_hz / 1000), M[s->mod], (unsigned long)s->baud, (unsigned long)s->dev_hz,
            s->dbm, s->pa, s->tx ? "true" : "false", s->rx ? "true" : "false",
            cc1101_marcstate(r), s->rx ? cc1101_rssi_dbm(r) : 0);
    }
    n += snprintf(out + n, sz > n ? sz - n : 0, "]}");
    return n;
}

/* Attend que la MARCSTATE atteigne l'etat vise (calibration, PLL), au plus ms. */
static uint8_t wait_marc(int r, uint8_t want, int ms)
{
    uint8_t m = cc1101_marcstate(r);
    for (int i = 0; i < ms / 5 && m != want; i++) { vTaskDelay(pdMS_TO_TICKS(5)); m = cc1101_marcstate(r); }
    return m;
}

static int ctrl_selftest_l(ctrl_check_cb_t cb, void *arg)
{
    int fail = 0;
#define CHECK(cond, label) do { bool _c = (cond); cb(RADIOS[r].band, label, _c, arg); if (!_c) fail++; } while (0)
    for (int r = 0; r < BOARD_RADIO_COUNT; r++) {
        s_cur = r;
        uint8_t p, v;
        CHECK(cc1101_present(r, &p, &v) && v != 0x00, "CC1101 present");
        ctrl_tx(false);
        CHECK(wait_marc(r, 0x01, 100) == 0x01, "repos, MARCSTATE 0x01");
        ctrl_tx(true);
        CHECK(wait_marc(r, 0x13, 200) == 0x13, "emission, MARCSTATE 0x13");
        ctrl_tx(false);
        ctrl_rx(true);
        CHECK(wait_marc(r, 0x0D, 200) == 0x0D, "ecoute, MARCSTATE 0x0D");
        int rssi = cc1101_rssi_dbm(r);
        CHECK(rssi < 0 && rssi > -140, "RSSI lisible");
        ctrl_rx(false);
        CHECK(ctrl_set_freq_hz(100000u * 1000u) != NULL, "frequence hors plage refusee");
        CHECK(ctrl_set_power(30) != NULL, "puissance au-dela du declare refusee");
    }
    s_cur = 0;
    return fail;
#undef CHECK
}


/* --- Enrobages verrouilles : point d'entree public de chaque reglage. --- */
const char *ctrl_select(const char *band)
{ CTRL_LOCK(); const char * r = ctrl_select_l(band); CTRL_UNLOCK(); return r; }
const char *ctrl_set_freq_hz(uint32_t hz)
{ CTRL_LOCK(); const char * r = ctrl_set_freq_hz_l(hz); CTRL_UNLOCK(); return r; }
const char *ctrl_set_chan(const char *w)
{ CTRL_LOCK(); const char * r = ctrl_set_chan_l(w); CTRL_UNLOCK(); return r; }
const char *ctrl_set_mod(const char *n)
{ CTRL_LOCK(); const char * r = ctrl_set_mod_l(n); CTRL_UNLOCK(); return r; }
const char *ctrl_set_rate(uint32_t baud)
{ CTRL_LOCK(); const char * r = ctrl_set_rate_l(baud); CTRL_UNLOCK(); return r; }
const char *ctrl_set_dev(uint32_t hz)
{ CTRL_LOCK(); const char * r = ctrl_set_dev_l(hz); CTRL_UNLOCK(); return r; }
const char *ctrl_set_power(int dbm)
{ CTRL_LOCK(); const char * r = ctrl_set_power_l(dbm); CTRL_UNLOCK(); return r; }
const char *ctrl_set_pa(unsigned pa)
{ CTRL_LOCK(); const char * r = ctrl_set_pa_l(pa); CTRL_UNLOCK(); return r; }
const char *ctrl_tx(bool on)
{ CTRL_LOCK(); const char * r = ctrl_tx_l(on); CTRL_UNLOCK(); return r; }
const char *ctrl_rx(bool on)
{ CTRL_LOCK(); const char * r = ctrl_rx_l(on); CTRL_UNLOCK(); return r; }
int ctrl_selftest(ctrl_check_cb_t cb, void *arg)
{ CTRL_LOCK(); int r = ctrl_selftest_l(cb, arg); CTRL_UNLOCK(); return r; }
size_t ctrl_state_json(char *out, size_t sz)
{ CTRL_LOCK(); size_t r = ctrl_state_json_l(out, sz); CTRL_UNLOCK(); return r; }
