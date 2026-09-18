/*
 * cc1101 : l'acces SPI a chaque radio de la carte, et les etats de test.
 *
 * Deux CC1101 sur le meme bus, chacun avec son CS et ses GDO, tels que la
 * declaration de carte les donne (board_pins.h, engendre). Un seul emetteur
 * actif a la fois : passer une radio en emission met l'autre au repos.
 */
#ifndef CC1101_H
#define CC1101_H

#include <stdbool.h>
#include <stdint.h>
#include "rf_regs.h"

typedef struct {
    uint32_t freq_hz;
    rf_mod_t mod;
    uint32_t baud;
    uint32_t dev_hz;
    int dbm;          /* puissance demandee, -128 = valeur brute */
    uint8_t pa;       /* valeur PATABLE effectivement ecrite */
    bool tx, rx;
} cc1101_state_t;

int  cc1101_setup(void);                          /* bus SPI et les CS, GDO en entree */
bool cc1101_present(int radio, uint8_t *partnum, uint8_t *version);
/* Applique la configuration et lance : emission continue (tx) ou ecoute (rx).
 * Rend false et n'ecrit rien si un registre ne se calcule pas. */
bool cc1101_apply(int radio, const cc1101_state_t *st);
void cc1101_idle(int radio);
uint8_t cc1101_marcstate(int radio);
int  cc1101_rssi_dbm(int radio);
uint8_t cc1101_read(int radio, uint8_t addr);

#endif
