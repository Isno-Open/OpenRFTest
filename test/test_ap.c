/* Le nom du reseau et le mot de passe, sans puce. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/ap.h"

static int n = 0;
#define CHECK(cond, msg) do { n++; if (!(cond)) { printf("ECHEC %s\n", msg); exit(1); } } while (0)

int main(void)
{
    char ssid[33];
    unsigned char mac[6] = { 0x10, 0x20, 0x30, 0x40, 0xAB, 0xCD };
    CHECK(ap_ssid_build(mac, "Super", ssid, sizeof ssid) && strcmp(ssid, "OpenRFTest-Super-ABCD") == 0, "nom du reseau");
    char small[8];
    CHECK(!ap_ssid_build(mac, "Super", small, sizeof small), "tampon trop court refuse");

    char pw[AP_PASSWORD_LEN + 1];
    unsigned char rnd[AP_PASSWORD_LEN] = { 0, 1, 2, 30, 31, 32, 255, 254, 100, 50, 7, 9 };
    ap_password_from_random(rnd, pw, sizeof pw);
    CHECK(strlen(pw) == AP_PASSWORD_LEN, "12 caracteres");
    CHECK(strpbrk(pw, "0O1Il") == NULL, "aucun caractere ambigu");
    /* 31 = 0 modulo 31 : les octets 0 et 31 donnent le meme symbole, pas de plantage. */
    CHECK(pw[0] == pw[4], "modulo sur l'alphabet");
    printf("%d verifications\n", n);
    return 0;
}
