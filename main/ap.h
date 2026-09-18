/*
 * ap : le point d'acces Wi-Fi du firmware de test, et son mot de passe.
 *
 * Meme regle que le launcher ISNO, pour la meme raison (EN 18031-1, mot de
 * passe non facultatif) : WPA2 obligatoire, mot de passe tire au sort au premier
 * demarrage, range en NVS, affiche sur la console. Jamais de repli ouvert.
 * Nom : « OpenRFTest-<slug>-<4 hex de la MAC> ».
 */
#ifndef AP_H
#define AP_H
#include <stdbool.h>
#include <stddef.h>

#define AP_PASSWORD_LEN 12
/* Engendre un mot de passe depuis 12 octets d'alea, sur un alphabet sans
 * caracteres ambigus. Fonction pure, testee sur poste. */
void ap_password_from_random(const unsigned char rnd[AP_PASSWORD_LEN], char *out, size_t sz);
/* Construit le nom du reseau. Fonction pure. */
bool ap_ssid_build(const unsigned char mac[6], const char *slug, char *out, size_t sz);
/* Leve le point d'acces. Rend false sans mot de passe. */
bool ap_start(const char *slug);
#endif
