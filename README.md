# OpenRFTest

Un firmware de test radio pour ESP32 et CC1101 : il met **une** radio dans un état fixe
et l'y laisse. Émission continue, porteuse pure ou modulée, ou écoute continue. C'est
ce qu'un laboratoire attend pour mesurer une carte, et ce qu'un banc attend aussi.

Il est né pour la carte [ISNO Super](https://www.isno.fr/liste-attente), deux CC1101 en
433 et 868 MHz sur une seule antenne, et il tourne aussi sur un M5Stack ATOM Lite avec
un module CC1101 en Dupont, le banc d'[OpenProfalux](https://github.com/Isno-Open/OpenProfalux).

## Ce qu'il fait, et rien d'autre

| Commande | Effet |
|---|---|
| `radio 868` ou `radio 433` | choisit la radio ; l'autre est mise au repos |
| `freq <kHz>`, `chan low\|mid\|high` | fréquence, **refusée hors de la plage déclarée** pour cette radio |
| `mod cw\|ook\|2fsk` | porteuse pure, OOK, 2-FSK |
| `rate <bauds>`, `dev <Hz>` | débit des données aléatoires et déviation 2-FSK |
| `power <dBm>` | valeur de la table de la fiche technique, **refusée au-delà de la puissance déclarée** |
| `pa 0xNN` | valeur PATABLE brute, pour les puissances hors table (8, 9 dBm) : à mesurer au banc |
| `tx on\|off` | émission continue, données aléatoires (le mode « continuous TX » de SmartRF Studio) |
| `rx on\|off`, `rssi` | écoute continue et niveau reçu |
| `status`, `id` | état des radios ; présence des CC1101 par leurs registres d'identité |
| `selftest` | auto-test fonctionnel sans instrument : présence, transitions repos/TX/RX, RSSI, refus hors déclaration ; verdict PASS/FAIL |

Un seul émetteur actif à la fois. La LED, quand la carte en a une, est allumée pendant
l'émission.

Deux façons de le commander, qui font exactement la même chose (`main/ctrl.c`) :

- **le port série** : l'USB natif sur l'ESP32-S3, l'UART du pont USB sur l'ESP32 ;
- **la page locale**, sur le point d'accès Wi-Fi de la carte, `http://192.168.4.1/`.

Le point d'accès s'appelle `OpenRFTest-<carte>-<4 hex>` et il est en WPA2, avec un mot de
passe tiré au sort au premier démarrage, rangé en NVS et **affiché sur la console** à
chaque démarrage. Jamais de point d'accès ouvert : c'est la règle de l'EN 18031-1, et
elle vaut aussi pour un firmware de test.

## D'où viennent les limites

De `boards/<carte>.json`, la déclaration de carte : broches SPI, CS et GDO de chaque
radio, plage autorisée, porteuse par défaut, puissance conduite maximale. CMake engendre
`board_pins.h` à la configuration, dans le dossier de construction ; rien d'engendré
n'est commis, et le firmware ne connaît aucune de ces valeurs. Une autre carte ou une
autre région, c'est une autre déclaration.

| Carte | Fichier | Cible | Console |
|---|---|---|---|
| ISNO Super, deux CC1101 | `boards/isno-super.json` | `esp32s3` | USB natif |
| M5Stack ATOM Lite + CC1101 | `boards/m5-atom-lite.json` | `esp32` | UART |

`boards/isno-super.json` est une copie de la déclaration du launcher ISNO, qui en est la
source ; la CI du launcher compare les deux et refuse la divergence. Ne pas la modifier ici.

Sur la Super, OpenRFTest est une image du catalogue : le launcher l'installe dans un
emplacement applicatif et la lance ; elle confirme son démarrage, et un reset rend la
main au launcher.

## L'API de la page

`GET /api/state` rend l'état des radios en JSON. `POST /api/set` applique, dans l'ordre,
les clefs présentes : `radio`, `freq_khz`, `chan`, `mod`, `baud`, `dev_hz`, `dbm`, `pa` ;
le premier refus arrête et revient dans `message`. `POST /api/tx` et `POST /api/rx`
prennent `{"on": true|false}`. `POST /api/selftest` lance l'auto-test et rend chaque
vérification, `{radio, label, pass}`, avec le total ; la page a son bouton.

## Ce qui est vérifié sans matériel

`test/run.sh` compile les parties pures sur le poste : les registres du CC1101
recalculés depuis les grandeurs physiques et confrontés à des valeurs connues d'ailleurs
(le mot de fréquence de 433,92 MHz des télécommandes, le débit 38,4 kbauds et la
déviation 19 kHz d'io-homecontrol, la déviation 5,2 kHz d'OpenProfalux, la table PATABLE
de la fiche technique, les refus hors plage et hors puissance), le nom et le mot de
passe du réseau, et le générateur d'en-tête sur chaque carte, avec un refus éprouvé.
La CI les exécute, puis compile les deux cibles.

## Ce qui n'a pas encore tourné sur une carte

Rien. Les cinq prototypes de la Super arrivent fin septembre 2026. Première mesure à
faire : `id` doit voir les deux CC1101 ; puis `tx on` en porteuse sur la porteuse
déclarée, et la puissance conduite à l'analyseur par le commutateur de test. C'est là que
les valeurs PATABLE intermédiaires se calent.

## Construire

```bash
# ISNO Super
docker run --rm -v "$PWD":/w -w /w -u $(id -u):$(id -g) -e HOME=/tmp espressif/idf:v6.1 \
  idf.py -B build/super -DSDKCONFIG=build/super/sdkconfig -DIDF_TARGET=esp32s3 -DBOARD=isno-super build
# M5Stack ATOM Lite
docker run --rm -v "$PWD":/w -w /w -u $(id -u):$(id -g) -e HOME=/tmp espressif/idf:v6.1 \
  idf.py -B build/atom -DSDKCONFIG=build/atom/sdkconfig -DIDF_TARGET=esp32 -DBOARD=m5-atom-lite build
```

Mesuré le 2026-09-18 pour la Super : `0xdd650`, 907 Ko ; 30 % de libre dans un
emplacement du launcher.

## Premier essai réel : l'ATOM Lite et un module CC1101

Le module se câble comme pour OpenProfalux, sur le connecteur du bas de l'ATOM Lite,
par nom de signal : SCK G19, MISO G33, MOSI G23, CS G22, GDO0 G25, GDO2 G21, 3V3 et GND.
Jamais 5 V sur le CC1101.

1. Construire et flasher, l'ATOM branché en USB (le port est `/dev/ttyUSB0` ou
   `/dev/ttyACM0`, `ls /dev/tty*` le dit) :

   ```bash
   docker run --rm -v "$PWD":/w -w /w -u $(id -u):$(id -g) -e HOME=/tmp \
     --device /dev/ttyUSB0 --group-add dialout espressif/idf:v6.1 \
     idf.py -B build/atom -DSDKCONFIG=build/atom/sdkconfig -DIDF_TARGET=esp32 -DBOARD=m5-atom-lite \
     -p /dev/ttyUSB0 flash monitor
   ```

   Le moniteur affiche le nom du point d'accès et son mot de passe, puis
   `OpenRFTest sur M5Stack ATOM Lite + CC1101 : 1 radio(s) declaree(s)`, et le résultat
   de `id`. Sortir du moniteur : `Ctrl+]`.
2. `id` doit dire `radio 868 : PARTNUM 0x00 VERSION 0x04 : CC1101 present` (`VERSION`
   vaut `0x04` ou `0x14` selon le lot de la puce ; seul `0x00` = puce muette). Sinon,
   c'est le câblage : relire les fils par nom de signal, `CS` sur `CSN`.
3. `tx on` : la radio émet une porteuse à 868,35 MHz, à 0 dBm de la table. Sur
   l'analyseur, une raie fine à cette fréquence. `status` montre `MARCSTATE 0x13`, l'état
   TX de la puce.
4. `power 10`, puis `pa 0xC5` par exemple : c'est ainsi que se calent les valeurs PATABLE
   intermédiaires, en lisant la puissance à l'analyseur. Noter la correspondance mesurée
   dans `boards/`, pas dans le code.
5. `mod ook`, `mod 2fsk` avec `rate 38400` et `dev 19040` : la raie s'élargit selon la
   modulation. `tx off` pour arrêter.
6. Sur le téléphone, rejoindre `OpenRFTest-Atom-xxxx` avec le mot de passe affiché, puis
   `http://192.168.4.1/` : la page fait la même chose que la console.

Raccourci : `selftest` enchaîne présence, transitions repos/TX/RX, lecture RSSI et refus
hors déclaration, et sort un verdict `PASS/FAIL` unique. Il ne mesure pas la puissance
conduite : ça, c'est l'analyseur, aux points 4 et suivants.

## Licence

GPL-3.0-or-later. Projet [Isno-Open](https://github.com/Isno-Open).
