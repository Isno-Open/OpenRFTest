#include "rf_regs.h"
#include <math.h>

uint32_t rf_freq_word(uint32_t hz)
{
    /* f = FREQ * f_xosc / 2^16  (SWRS061I, 21.1) */
    double w = (double)hz * 65536.0 / (double)RF_XTAL_HZ;
    return (uint32_t)(w + 0.5) & 0x3FFFFF;
}

double rf_datarate_actual(uint8_t drate_e, uint8_t drate_m)
{
    /* R = (256 + M) * 2^E * f_xosc / 2^28  (12.1) */
    return (256.0 + drate_m) * ldexp(1.0, drate_e) * (double)RF_XTAL_HZ / 268435456.0;
}

bool rf_datarate_regs(uint32_t baud, uint8_t *drate_e, uint8_t *drate_m)
{
    if (baud < 600 || baud > 500000) return false;
    double x = (double)baud * 268435456.0 / (double)RF_XTAL_HZ; /* = (256+M) * 2^E */
    int e = (int)floor(log2(x)) - 8;
    if (e < 0) e = 0;
    double m = x / ldexp(1.0, e) - 256.0;
    int mi = (int)(m + 0.5);
    if (mi >= 256) { e += 1; mi = 0; }
    if (e > 15) return false;
    *drate_e = (uint8_t)e;
    *drate_m = (uint8_t)mi;
    return true;
}

double rf_deviation_actual(uint8_t deviatn)
{
    /* f_dev = f_xosc / 2^17 * (8 + M) * 2^E  (16.1), M = bits 2:0, E = bits 6:4 */
    int m = deviatn & 0x07, e = (deviatn >> 4) & 0x07;
    return (double)RF_XTAL_HZ / 131072.0 * (8.0 + m) * ldexp(1.0, e);
}

bool rf_deviation_reg(uint32_t hz, uint8_t *deviatn)
{
    /* On cherche le couple (E, M) le plus proche ; la plage va de 1,6 a 381 kHz. */
    double best = 1e12; int be = -1, bm = -1;
    for (int e = 0; e < 8; e++)
        for (int m = 0; m < 8; m++) {
            double d = fabs(rf_deviation_actual((uint8_t)((e << 4) | m)) - (double)hz);
            if (d < best) { best = d; be = e; bm = m; }
        }
    if (be < 0) return false;
    /* Au-dela de 5 % d'ecart on refuse : l'appelant voulait autre chose. */
    double got = rf_deviation_actual((uint8_t)((be << 4) | bm));
    if (fabs(got - (double)hz) > 0.05 * (double)hz + 1.0) return false;
    *deviatn = (uint8_t)((be << 4) | bm);
    return true;
}

/*
 * SWRS061I, table 39 « Optimum PATABLE settings for various output power
 * levels », quartz 26 MHz. Les puissances intermediaires (8, 9 dBm) ne sont pas
 * dans la table : elles se reglent par une valeur brute mesuree au banc.
 */
static const struct { int dbm; uint8_t pa433, pa868, pa915; } PA[] = {
    { -30, 0x12, 0x03, 0x03 },
    { -20, 0x0E, 0x0F, 0x0E },
    { -15, 0x1D, 0x1E, 0x1E },
    { -10, 0x34, 0x27, 0x27 },
    {   0, 0x60, 0x50, 0x8E },
    {   5, 0x84, 0x81, 0xCD },
    {   7, 0xC8, 0xCB, 0xC7 },
    {  10, 0xC0, 0xC2, 0xC0 },
};

bool rf_patable(int band_mhz, int dbm, uint8_t *pa)
{
    for (unsigned i = 0; i < sizeof PA / sizeof PA[0]; i++) {
        if (PA[i].dbm != dbm) continue;
        if (band_mhz < 600)       *pa = PA[i].pa433;
        else if (band_mhz < 890)  *pa = PA[i].pa868;
        else                      *pa = PA[i].pa915;
        return true;
    }
    return false;
}

uint8_t rf_mdmcfg2(rf_mod_t mod)
{
    /* MOD_FORMAT bits 6:4 : 000 2-FSK, 011 ASK/OOK ; SYNC_MODE 000 : rien. */
    switch (mod) {
    case RF_MOD_OOK:  return 0x30;
    case RF_MOD_2FSK: return 0x00;
    case RF_MOD_CW:   return 0x00;
    }
    return 0x00;
}

bool rf_in_band(uint32_t hz, const rf_band_t *band)
{
    uint32_t khz = (hz + 500) / 1000;
    return khz >= (uint32_t)band->low_khz && khz <= (uint32_t)band->high_khz;
}

bool rf_power_allowed(int dbm, const rf_band_t *band)
{
    return dbm <= band->max_dbm;
}
