/*
 * rf_regs : les registres du CC1101 calcules depuis des grandeurs physiques.
 *
 * FONCTIONS PURES, SANS PUCE NI ESP-IDF : elles se testent sur poste
 * (test/run.sh). Les formules sont celles de la fiche technique CC1101
 * (SWRS061I), sections 12 (data rate), 16 (deviation) et 21 (frequence),
 * pour un quartz de 26 MHz. Les valeurs ne sont pas recopiees d'un outil :
 * elles se recalculent, et les temoins les confrontent a des registres connus.
 */
#ifndef RF_REGS_H
#define RF_REGS_H

#include <stdbool.h>
#include <stdint.h>

#define RF_XTAL_HZ 26000000UL

typedef enum {
    RF_MOD_CW = 0,   /* porteuse pure : 2-FSK a deviation nulle, donnees aleatoires */
    RF_MOD_OOK,
    RF_MOD_2FSK,
} rf_mod_t;

/* Une plage autorisee, telle que la declaration de carte la donne. */
typedef struct {
    int low_khz, high_khz;
    int max_dbm;
} rf_band_t;

/* FREQ2:FREQ1:FREQ0 pour une frequence en Hz. */
uint32_t rf_freq_word(uint32_t hz);
/* MDMCFG4 (quartet bas, DRATE_E) et MDMCFG3 (DRATE_M) pour un debit en bauds.
 * Rend false si le debit est hors de ce que la puce sait faire. */
bool rf_datarate_regs(uint32_t baud, uint8_t *drate_e, uint8_t *drate_m);
/* Le debit reellement obtenu pour un couple (E, M), en bauds. */
double rf_datarate_actual(uint8_t drate_e, uint8_t drate_m);
/* DEVIATN pour une deviation en Hz. Rend false si hors plage. */
bool rf_deviation_reg(uint32_t hz, uint8_t *deviatn);
double rf_deviation_actual(uint8_t deviatn);
/* Valeur PATABLE optimale de la fiche technique (table 39) pour une bande et
 * une puissance en dBm. Rend false si la puissance n'est pas dans la table. */
bool rf_patable(int band_mhz, int dbm, uint8_t *pa);
/* MDMCFG2 : format de modulation, sans preambule ni mot de synchronisation. */
uint8_t rf_mdmcfg2(rf_mod_t mod);
/* La frequence est-elle dans la plage declaree, bornes incluses ? */
bool rf_in_band(uint32_t hz, const rf_band_t *band);
/* La puissance est-elle au plus la puissance declaree ? */
bool rf_power_allowed(int dbm, const rf_band_t *band);

#endif
