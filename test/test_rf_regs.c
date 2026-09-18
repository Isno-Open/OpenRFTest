/* Les registres recalcules, confrontes a des valeurs connues d'ailleurs. */
#include <stdio.h>
#include <stdlib.h>
#include "../main/rf_regs.h"

static int n = 0;
#define CHECK(cond, msg) do { n++; if (!(cond)) { printf("ECHEC %s\n", msg); exit(1); } } while (0)

int main(void)
{
    /* 433,92 MHz -> 0x10B071 : le mot de frequence le plus repandu des telecommandes. */
    CHECK(rf_freq_word(433920000) == 0x10B071, "433,92 MHz");
    /* 868,3 MHz -> 0x21656A (SmartRF Studio). */
    CHECK(rf_freq_word(868300000) == 0x21656A, "868,3 MHz");
    /* Et la formule inversee retombe sur la frequence a 400 Hz pres (le pas du registre). */
    CHECK(abs((int)((double)rf_freq_word(868350000) * RF_XTAL_HZ / 65536.0) - 868350000) < 400, "868,35 MHz inverse");
    CHECK(abs((int)((double)rf_freq_word(915000000) * RF_XTAL_HZ / 65536.0) - 915000000) < 400, "915 MHz inverse");

    uint8_t e, m;
    /* 38,4 kbit/s d io-homecontrol : DRATE_E = 0xA, DRATE_M = 0x83 (iohc-flipper). */
    CHECK(rf_datarate_regs(38400, &e, &m) && e == 0x0A && m == 0x83, "38,4 kbauds");
    /* 1,2 kbaud, defaut SmartRF : MDMCFG4 = 0xF5, MDMCFG3 = 0x83. */
    CHECK(rf_datarate_regs(1200, &e, &m) && e == 0x05 && m == 0x83, "1,2 kbaud");
    CHECK(!rf_datarate_regs(100, &e, &m), "100 bauds refuse");

    uint8_t dv;
    /* 19,04 kHz -> 0x34 (iohc-flipper) ; 5,157 kHz -> 0x15 (OpenProfalux). */
    CHECK(rf_deviation_reg(19040, &dv) && dv == 0x34, "deviation 19 kHz");
    CHECK(rf_deviation_reg(5157, &dv) && dv == 0x15, "deviation 5,2 kHz");
    CHECK(!rf_deviation_reg(900000, &dv), "deviation 900 kHz refusee");

    uint8_t pa;
    CHECK(rf_patable(433, 10, &pa) && pa == 0xC0, "PATABLE 433 +10");
    CHECK(rf_patable(868, 10, &pa) && pa == 0xC2, "PATABLE 868 +10");
    CHECK(rf_patable(915, 0, &pa) && pa == 0x8E, "PATABLE 915 0");
    CHECK(!rf_patable(868, 9, &pa), "9 dBm hors table : valeur brute a mesurer");

    CHECK(rf_mdmcfg2(RF_MOD_OOK) == 0x30 && rf_mdmcfg2(RF_MOD_2FSK) == 0x00, "MDMCFG2");

    /* La declaration de carte, Europe : ce qui sort de la plage est refuse. */
    rf_band_t eu868 = { 863000, 870000, 10 };
    CHECK(rf_in_band(868350000, &eu868), "868,35 dans 863-870");
    CHECK(rf_in_band(870000000, &eu868) && rf_in_band(863000000, &eu868), "bornes incluses");
    CHECK(!rf_in_band(915000000, &eu868), "915 refuse en Europe");
    CHECK(!rf_in_band(870001000, &eu868), "870,001 refuse");
    CHECK(rf_power_allowed(10, &eu868) && !rf_power_allowed(12, &eu868), "12 dBm refuse");

    printf("%d verifications\n", n);
    return 0;
}
