/*
 * ctrl : l'etat des radios et les reglages, communs a la console et a la page.
 *
 * Chaque reglage rend NULL si accepte, sinon un message de refus en francais,
 * pret a afficher. Les limites (plage, puissance) viennent de la declaration de
 * carte via board_pins.h ; ce module ne les connait pas autrement.
 */
#ifndef CTRL_H
#define CTRL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cc1101.h"

void ctrl_init(void);
int  ctrl_radio_count(void);
const char *ctrl_band_name(int radio);
int  ctrl_current(void);

const char *ctrl_select(const char *band);
const char *ctrl_set_freq_hz(uint32_t hz);
const char *ctrl_set_chan(const char *which);          /* low | mid | high */
const char *ctrl_set_mod(const char *name);            /* cw | ook | 2fsk */
const char *ctrl_set_rate(uint32_t baud);
const char *ctrl_set_dev(uint32_t hz);
const char *ctrl_set_power(int dbm);
const char *ctrl_set_pa(unsigned pa);
const char *ctrl_tx(bool on);
const char *ctrl_rx(bool on);

const cc1101_state_t *ctrl_state(int radio);
/* L'etat complet en JSON, pour la page : radios, reglages, presence, RSSI. */
size_t ctrl_state_json(char *out, size_t sz);

#endif
