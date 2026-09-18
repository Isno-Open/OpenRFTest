#include "cc1101.h"
#include "board_pins.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "cc1101";
static const board_radio_t RADIOS[BOARD_RADIO_COUNT] = BOARD_RADIOS_INIT;
static spi_device_handle_t s_dev[BOARD_RADIO_COUNT];

/* Registres (SWRS061I, table 43) */
enum { IOCFG2 = 0x00, IOCFG0 = 0x02, FIFOTHR = 0x03, PKTLEN = 0x06, PKTCTRL1 = 0x07,
       PKTCTRL0 = 0x08, FSCTRL1 = 0x0B, FSCTRL0 = 0x0C, FREQ2 = 0x0D, FREQ1 = 0x0E,
       FREQ0 = 0x0F, MDMCFG4 = 0x10, MDMCFG3 = 0x11, MDMCFG2 = 0x12, MDMCFG1 = 0x13,
       MDMCFG0 = 0x14, DEVIATN = 0x15, MCSM1 = 0x17, MCSM0 = 0x18, FOCCFG = 0x19,
       AGCCTRL2 = 0x1B, WORCTRL = 0x20, FREND1 = 0x21, FREND0 = 0x22, FSCAL3 = 0x23,
       FSCAL2 = 0x24, FSCAL1 = 0x25, FSCAL0 = 0x26, TEST2 = 0x2C, TEST1 = 0x2D,
       TEST0 = 0x2E, PARTNUM = 0x30, VERSION = 0x31, RSSI = 0x34, MARCSTATE = 0x35,
       PATABLE = 0x3E };
enum { SRES = 0x30, SRX = 0x34, STX = 0x35, SIDLE = 0x36, SFRX = 0x3A, SFTX = 0x3B };

static void xfer(int r, uint8_t *tx, uint8_t *rx, size_t n)
{
    spi_transaction_t t = { .length = 8 * n, .tx_buffer = tx, .rx_buffer = rx };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_dev[r], &t));
}

static void wr(int r, uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = { addr, val }, rx[2];
    xfer(r, tx, rx, 2);
}

uint8_t cc1101_read(int r, uint8_t addr)
{
    /* Les registres d'etat (0x30..0x3D) se lisent en mode « burst » (0xC0). */
    uint8_t tx[2] = { (uint8_t)(addr | (addr >= 0x30 ? 0xC0 : 0x80)), 0 }, rx[2];
    xfer(r, tx, rx, 2);
    return rx[1];
}

static void strobe(int r, uint8_t s)
{
    uint8_t tx[1] = { s }, rx[1];
    xfer(r, tx, rx, 1);
}

int cc1101_setup(void)
{
    spi_bus_config_t bus = {
        .sclk_io_num = BOARD_PIN_SPI_SCK, .miso_io_num = BOARD_PIN_SPI_MISO,
        .mosi_io_num = BOARD_PIN_SPI_MOSI, .quadwp_io_num = -1, .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));
    for (int r = 0; r < BOARD_RADIO_COUNT; r++) {
        spi_device_interface_config_t dev = {
            .clock_speed_hz = 1000000, .mode = 0, .spics_io_num = RADIOS[r].cs, .queue_size = 1,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_dev[r]));
        gpio_set_direction(RADIOS[r].gdo0, GPIO_MODE_INPUT);
        gpio_set_direction(RADIOS[r].gdo2, GPIO_MODE_INPUT);
        strobe(r, SRES);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return 0;
}

bool cc1101_present(int r, uint8_t *partnum, uint8_t *version)
{
    *partnum = cc1101_read(r, PARTNUM);
    *version = cc1101_read(r, VERSION);
    /* PARTNUM 0x00, VERSION 0x04 ou 0x14 sur les CC1101 en circulation. Un bus
       sans puce lit 0x00/0x00 ou 0xFF/0xFF. */
    return *partnum == 0x00 && (*version == 0x04 || *version == 0x14);
}

void cc1101_idle(int r)
{
    strobe(r, SIDLE);
    strobe(r, SFTX);
    strobe(r, SFRX);
}

bool cc1101_apply(int r, const cc1101_state_t *st)
{
    uint8_t e, m, dv;
    if (!rf_datarate_regs(st->baud, &e, &m)) { ESP_LOGE(TAG, "debit %lu hors plage", (unsigned long)st->baud); return false; }
    uint32_t dev = st->mod == RF_MOD_CW ? 0 : st->dev_hz;
    if (dev == 0) dv = 0x00;  /* deviation minimale : 1,6 kHz, la porteuse a toutes fins utiles */
    else if (!rf_deviation_reg(dev, &dv)) { ESP_LOGE(TAG, "deviation %lu hors plage", (unsigned long)dev); return false; }
    uint32_t fw = rf_freq_word(st->freq_hz);

    /* Un seul emetteur actif : l'autre radio au repos avant de toucher a celle-ci. */
    for (int o = 0; o < BOARD_RADIO_COUNT; o++) if (o != r) cc1101_idle(o);
    cc1101_idle(r);

    wr(r, IOCFG2, 0x2E); wr(r, IOCFG0, 0x2E);   /* GDO en haute impedance */
    wr(r, FIFOTHR, 0x47);
    wr(r, PKTLEN, 0xFF); wr(r, PKTCTRL1, 0x00);
    /* PKTCTRL0 = 0x22 : donnees aleatoires en emission, paquet infini, sans CRC.
       C'est le mode « continuous TX modulated » de SmartRF Studio. */
    wr(r, PKTCTRL0, st->tx ? 0x22 : 0x02);
    wr(r, FSCTRL1, 0x06); wr(r, FSCTRL0, 0x00);
    wr(r, FREQ2, (uint8_t)(fw >> 16)); wr(r, FREQ1, (uint8_t)(fw >> 8)); wr(r, FREQ0, (uint8_t)fw);
    wr(r, MDMCFG4, (uint8_t)(0xC0 | e));       /* bande RX 102 kHz, DRATE_E */
    wr(r, MDMCFG3, m);
    wr(r, MDMCFG2, rf_mdmcfg2(st->mod));
    wr(r, MDMCFG1, 0x22); wr(r, MDMCFG0, 0xF8);
    wr(r, DEVIATN, dv);
    wr(r, MCSM1, st->rx ? 0x0C : 0x00);         /* RX : rester en RX ; TX : sans objet, paquet infini */
    wr(r, MCSM0, 0x18);                          /* calibration a IDLE -> TX/RX */
    wr(r, FOCCFG, 0x16); wr(r, AGCCTRL2, 0x43); wr(r, WORCTRL, 0xFB);
    wr(r, FREND1, st->mod == RF_MOD_OOK ? 0xB6 : 0x56);
    /* OOK : PA_POWER = 1, la puce alterne PATABLE[0] (0) et PATABLE[1] (puissance).
       FSK et porteuse : PA_POWER = 0, PATABLE[0] = puissance. */
    wr(r, FREND0, st->mod == RF_MOD_OOK ? 0x11 : 0x10);
    wr(r, FSCAL3, 0xE9); wr(r, FSCAL2, 0x2A); wr(r, FSCAL1, 0x00); wr(r, FSCAL0, 0x1F);
    wr(r, TEST2, 0x81); wr(r, TEST1, 0x35); wr(r, TEST0, 0x09);
    {
        uint8_t tx[9] = { PATABLE | 0x40, 0 }, rx[9];
        if (st->mod == RF_MOD_OOK) { tx[1] = 0x00; tx[2] = st->pa; }
        else { tx[1] = st->pa; }
        xfer(r, tx, rx, 9);
    }
    if (st->tx) strobe(r, STX);
    else if (st->rx) strobe(r, SRX);
    vTaskDelay(pdMS_TO_TICKS(5));
    return true;
}

uint8_t cc1101_marcstate(int r) { return cc1101_read(r, MARCSTATE) & 0x1F; }

int cc1101_rssi_dbm(int r)
{
    int raw = (int8_t)cc1101_read(r, RSSI);
    return raw / 2 - 74;   /* SWRS061I 17.3, offset 74 dB */
}
