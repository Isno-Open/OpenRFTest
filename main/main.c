/*
 * OpenRFTest : le firmware de test radio, pour le banc et le laboratoire.
 *
 * Il met UNE radio CC1101 dans un etat fixe et l'y laisse : emission continue,
 * porteuse pure ou modulee, ou ecoute continue. Un seul emetteur actif a la
 * fois. Il refuse une frequence hors de la plage declaree pour la radio, et
 * une puissance au-dela de la puissance declaree. Ces limites, comme les
 * broches, viennent de board_pins.h, engendre depuis boards/<carte>.json :
 * le firmware n'en connait aucune.
 *
 * Deux facons de le commander, qui font exactement la meme chose (ctrl.c) :
 *   - le port serie (USB natif sur l'ESP32-S3, UART sur l'ESP32) ;
 *   - la page locale, sur son point d'acces Wi-Fi WPA2, http://192.168.4.1/.
 *
 * Sur une carte ISNO, c'est une image du catalogue : le launcher l'installe
 * dans un emplacement applicatif et la lance ; elle confirme son demarrage.
 *
 *   radio 868 | 433          choisir la radio (l'autre est mise au repos)
 *   freq <kHz>               frequence ; « chan low|mid|high » prend les bornes ou la porteuse
 *   mod cw | ook | 2fsk      porteuse pure, OOK, 2-FSK
 *   rate <bauds>             debit des donnees aleatoires (defaut 1200 ; 38400 pour io-homecontrol)
 *   dev <Hz>                 deviation 2-FSK (defaut 5157 ; 19040 pour io-homecontrol)
 *   power <dBm> | pa 0xNN    puissance de la table de la fiche technique, ou valeur PATABLE brute
 *   tx on | off              emission continue
 *   rx on | off              ecoute continue ; « rssi » lit le niveau
 *   status, id, help
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "board_pins.h"
#include "ctrl.h"
#include "web.h"

static void show(int r)
{
    const cc1101_state_t *s = ctrl_state(r);
    static const char *M[] = { "cw", "ook", "2fsk" };
    printf("radio %s : %s  %lu Hz  %s  %lu bauds  dev %lu Hz  pa 0x%02X (%s)  MARCSTATE 0x%02X\n",
           ctrl_band_name(r), s->tx ? "TX" : s->rx ? "RX" : "repos", (unsigned long)s->freq_hz, M[s->mod],
           (unsigned long)s->baud, (unsigned long)s->dev_hz, s->pa,
           s->dbm == -128 ? "brute" : "table", cc1101_marcstate(r));
}

static int done(const char *err) { if (err) { printf("%s\n", err); return 1; } show(ctrl_current()); return 0; }
#define ARG1(usage) if (argc != 2) { printf("%s\n", usage); return 1; }

static int cmd_radio(int argc, char **argv) { ARG1("radio <bande>"); return done(ctrl_select(argv[1])); }
static int cmd_freq(int argc, char **argv)  { ARG1("freq <kHz>"); return done(ctrl_set_freq_hz((uint32_t)strtoul(argv[1], NULL, 10) * 1000)); }
static int cmd_chan(int argc, char **argv)  { ARG1("chan low | mid | high"); return done(ctrl_set_chan(argv[1])); }
static int cmd_mod(int argc, char **argv)   { ARG1("mod cw | ook | 2fsk"); return done(ctrl_set_mod(argv[1])); }
static int cmd_rate(int argc, char **argv)  { ARG1("rate <bauds>"); return done(ctrl_set_rate(strtoul(argv[1], NULL, 10))); }
static int cmd_dev(int argc, char **argv)   { ARG1("dev <Hz>"); return done(ctrl_set_dev(strtoul(argv[1], NULL, 10))); }
static int cmd_power(int argc, char **argv) { ARG1("power <dBm>  (-30 -20 -15 -10 0 5 7 10)"); return done(ctrl_set_power(atoi(argv[1]))); }
static int cmd_pa(int argc, char **argv)    { ARG1("pa 0xNN  (valeur PATABLE brute, a mesurer au banc)"); return done(ctrl_set_pa(strtoul(argv[1], NULL, 0))); }
static int cmd_tx(int argc, char **argv)    { ARG1("tx on | off"); return done(ctrl_tx(strcmp(argv[1], "on") == 0)); }
static int cmd_rx(int argc, char **argv)    { ARG1("rx on | off"); return done(ctrl_rx(strcmp(argv[1], "on") == 0)); }

static int cmd_rssi(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("radio %s : RSSI %d dBm\n", ctrl_band_name(ctrl_current()), cc1101_rssi_dbm(ctrl_current()));
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int r = 0; r < ctrl_radio_count(); r++) show(r);
    printf("radio courante : %s\n", ctrl_band_name(ctrl_current()));
    return 0;
}

static int cmd_id(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int r = 0; r < ctrl_radio_count(); r++) {
        uint8_t p, v;
        bool ok = cc1101_present(r, &p, &v);
        printf("radio %s : PARTNUM 0x%02X VERSION 0x%02X : %s\n", ctrl_band_name(r), p, v,
               ok ? "CC1101 present" : "PAS de reponse (module absent ou bus muet)");
    }
    return 0;
}

static void reg(const char *n, const char *h, esp_console_cmd_func_t f)
{
    const esp_console_cmd_t c = { .command = n, .help = h, .func = f };
    ESP_ERROR_CHECK(esp_console_cmd_register(&c));
}

void app_main(void)
{
    /* Sur une carte ISNO, lancee par le launcher en mode « essayer » : on
       confirme le demarrage, sinon l'image est abandonnee au prochain reset.
       Sans table OTA (ATOM Lite), l'appel ne fait rien et le dit. */
    esp_ota_mark_app_valid_cancel_rollback();
    ctrl_init();
    if (!web_start()) ESP_LOGE("main", "page locale indisponible : la console reste");

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "rftest>";
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw, &rc, &repl));
#else
    esp_console_dev_uart_config_t hw = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw, &rc, &repl));
#endif
    esp_console_register_help_command();
    reg("radio", "radio <bande>", cmd_radio);
    reg("freq", "freq <kHz>, dans la plage declaree", cmd_freq);
    reg("chan", "chan low | mid | high", cmd_chan);
    reg("mod", "mod cw | ook | 2fsk", cmd_mod);
    reg("rate", "rate <bauds>", cmd_rate);
    reg("dev", "dev <Hz>", cmd_dev);
    reg("power", "power <dBm>, table de la fiche technique, au plus la puissance declaree", cmd_power);
    reg("pa", "pa 0xNN, valeur PATABLE brute", cmd_pa);
    reg("tx", "tx on | off, emission continue", cmd_tx);
    reg("rx", "rx on | off, ecoute continue", cmd_rx);
    reg("rssi", "niveau recu", cmd_rssi);
    reg("status", "etat des radios", cmd_status);
    reg("id", "presence des CC1101", cmd_id);
    printf("OpenRFTest sur %s : %d radio(s) declaree(s), aucune active. « help » pour les commandes.\n", BOARD_NAME, ctrl_radio_count());
    cmd_id(0, NULL);
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
