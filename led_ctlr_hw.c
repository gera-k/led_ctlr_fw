#include "nrfx_spim.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_error.h"
#include "nrf_log.h"

#include "app_error.h"
#include "app_util.h"
#include "app_timer.h"

#include "led_ctlr_hw.h"

// Brightness to PWM value lookup table
// PWM = ROUND(POWER(256,Brightness/256),0)-1
static const uint8_t Brightness2Pwm[] =
{
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1, 
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2, 
      2,   2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3, 
      3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   4,   4,   4,   4,   5,   5, 
      5,   5,   5,   5,   5,   5,   6,   6,   6,   6,   6,   6,   6,   7,   7,   7, 
      7,   7,   8,   8,   8,   8,   8,   9,   9,   9,   9,   9,  10,  10,  10,  10,
     11,  11,  11,  11,  12,  12,  12,  12,  13,  13,  13,  14,  14,  14,  15,  15,  
     15,  16,  16,  16,  17,  17,  18,  18,  18,  19,  19,  20,  20,  21,  21,  22,  
     22,  23,  23,  24,  24,  25,  25,  26,  26,  27,  28,  28,  29,  30,  30,  31,
     32,  32,  33,  34,  35,  35,  36,  37,  38,  39,  40,  40,  41,  42,  43,  44,
     45,  46,  47,  48,  49,  51,  52,  53,  54,  55,  56,  58,  59,  60,  62,  63,
     64,  66,  67,  69,  70,  72,  73,  75,  77,  78,  80,  82,  84,  86,  88,  90,
     91,  94,  96,  98, 100, 102, 104, 107, 109, 111, 114, 116, 119, 122, 124, 127,
    130, 133, 136, 139, 142, 145, 148, 151, 155, 158, 161, 165, 169, 172, 176, 180,
    184, 188, 192, 196, 201, 205, 210, 214, 219, 224, 229, 234, 239, 244, 250, 255
};

// multibyte bit set/clear
static void setBit(uint8_t* buf, uint8_t bit)
{
    uint8_t B = bit / 8;
    uint8_t b = 7 - (bit % 8);
    buf[B] |= 1 << b;
}

static void clrBit(uint8_t* buf, uint8_t bit)
{
    uint8_t B = bit / 8;
    uint8_t b = 7 - (bit % 8);
    buf[B] &= ~(1 << b);
}

// NeoPixel encoding
static int np_enc8(uint8_t data, uint8_t* buf)
{
    uint8_t pwm = Brightness2Pwm[data];
    uint8_t bit = 0;  // bit count in output buf
    for (int i=7; i>=0; i--)
    {
        if (pwm & (1<<i))
        {
            setBit(buf, bit++);
            setBit(buf, bit++);
            setBit(buf, bit++);
            clrBit(buf, bit++);
            clrBit(buf, bit++);
        }
        else
        {
            setBit(buf, bit++);
            clrBit(buf, bit++);
            clrBit(buf, bit++);
            clrBit(buf, bit++);
            clrBit(buf, bit++);
        }
    }
    return 5;
}

static int np_encRGB(uint8_t r, uint8_t g, uint8_t b, uint8_t* buf)
{
    np_enc8(g,  buf);
    np_enc8(r,  buf+5);
    np_enc8(b,  buf+10);
    return 15;
}

static int np_enc24(uint32_t data, uint8_t* buf)
{
    return np_encRGB((data & 0x00FF00) >> 8, (data & 0xFF0000) >> 16, (data & 0x0000FF) >> 0, buf);
}

#if defined(BOARD_PCA10056)
// 52840 DK supports legacy NeoPixel and Dotstar driver boards
//  connected to single SPI and GPIO pins

static int np_init(led_ctlr_hw_t* hw);
static int np_clear(led_ctlr_hw_t* hw);
static int np_show(led_ctlr_hw_t* hw, uint8_t row, uint32_t* buf, uint8_t len );

struct hw_NeoPixel
{
    struct led_ctlr_hw hw;

    nrfx_spim_t spi;                    // uses single SPI
    bool active;
    uint8_t sck;                        // SCK GPIO pin
    uint8_t mosi;                       // MOSI GPIO pin
    uint8_t row[4];                     // ROW OE pins (active low)
    nrfx_spim_xfer_desc_t xfer_desc;
    uint8_t buf[15*LS_MAX_LED_COUNT+2];    // NeoPixel mode = 15 bytes per LED plus two protecting bytes
} hw_NeoPixel = 
{
    .hw.mode = led_ctlr_NeoPixel,
    .hw.rows = 4,
    .hw.rows_per_refresh = 1,
    .hw.init = np_init,
    .hw.clear = np_clear,
    .hw.show = np_show,

    .spi = NRFX_SPIM_INSTANCE(0),
    .sck = 3,
    .mosi = 4,
    .row = {28, 29, 30, 31},
    .xfer_desc = NRFX_SPIM_XFER_TRX(hw_NeoPixel.buf, sizeof(hw_NeoPixel.buf), NULL, 0)
};

led_ctlr_hw_t* led_ctlr_create(led_ctlr_mode_t mode)
{
    switch(mode)
    {
    case led_ctlr_NeoPixel:
        return &hw_NeoPixel.hw;
    
    case led_ctlr_DotStar:
    default:
        break;
    }
    return NULL;
}

static void np_event_handler(nrfx_spim_evt_t const * p_event, void * p_context)
{
    struct hw_NeoPixel * np = (struct hw_NeoPixel*)p_context;

    for (int i=0; i<4; i++)
        nrf_gpio_pin_set(np->row[i]);

    np->active = false;
}

static int np_init(led_ctlr_hw_t* hw)
{
    struct hw_NeoPixel * np = CONTAINER_OF(hw, struct hw_NeoPixel, hw);

    nrfx_spim_config_t spi_config = NRFX_SPIM_DEFAULT_CONFIG;
    
    spi_config.frequency = NRF_SPIM_FREQ_4M;
    spi_config.mosi_pin = np->mosi;
    spi_config.sck_pin = np->sck;
    
    APP_ERROR_CHECK(nrfx_spim_init(&np->spi, &spi_config, np_event_handler, np));

    for (int i=0; i<4; i++)
    {
        nrf_gpio_cfg_output(np->row[i]);
        nrf_gpio_pin_set(np->row[i]);
    }

    np->active = false;

    return 0;
}

int np_clear(led_ctlr_hw_t* hw)
{
//    struct hw_NeoPixel * np = CONTAINER_OF(hw, struct hw_NeoPixel, hw);

    return 0;
}

int np_show(led_ctlr_hw_t* hw, uint8_t row, uint32_t* buf, uint8_t len)
{
    struct hw_NeoPixel * np = CONTAINER_OF(hw, struct hw_NeoPixel, hw);

    // select row in hardware
    for (int i=0; i<4; i++)
        nrf_gpio_pin_set(np->row[i]);
    nrf_gpio_pin_clear(np->row[row]);

    uint8_t* p = np->buf;
    size_t l = 1;
    *p++ = 0;
    for (int i = 0; i < len; i++)
    {
        int ll = np_enc24(buf[i], p);
        l += ll;
        p += ll;
    }
    p[l++] = 0;
    np->xfer_desc.tx_length = l;

    np->active = true;
    APP_ERROR_CHECK(nrfx_spim_xfer(&np->spi, &np->xfer_desc, 0));

    return 0;
}

#endif


#if defined(BOARD_PCA10059)
// 52840 USB dongle supports universal NeoPixel and Dotstar driver board
//  NeoPixel: up to 4 rows * LS_MAX_LED_COUNT LEDs
//      D1:  D: GPIO-1.15   OE: GPIO-0.02   SCK: 0.13
//      D2:  D: GPIO-0.29   OE: GPIO-0.31   SCK: 0.15
//      D3:  D: GPIO-0.22   OE: GPIO-0.20   SCK: 0.17
//      D4:  D: GPIO-1.00   OE: GPIO-0.24   SCK: 1.13

static int np_init(led_ctlr_hw_t* hw);
static int np_clear(led_ctlr_hw_t* hw);
static int np_show(led_ctlr_hw_t* hw, uint8_t row, uint32_t* buf, uint8_t len );

typedef struct _hw_np_row   // 4 rows on 4 individial SPI channels
{
    nrfx_spim_t spi;
    uint8_t sck;                            // SCK pin - not used but must be connected
    uint8_t mosi;                           // MOSI GPIO pin
    uint8_t oe;                             // driver OE pin
    uint8_t row;                            // own index
    bool active;
    nrfx_spim_xfer_desc_t xfer_desc;
    uint8_t buf[15*LS_MAX_LED_COUNT+2];     // NeoPixel mode = 15 bytes per LED plus two protecting bytes
} hw_np_row;

struct hw_NeoPixel
{
    struct led_ctlr_hw hw;

    hw_np_row row[4];

} hw_NeoPixel = 
{
    .hw.mode = led_ctlr_NeoPixel,
    .hw.rows = 4,
    .hw.rows_per_refresh = 4,
    .hw.init = np_init,
    .hw.clear = np_clear,
    .hw.show = np_show,

    .row = 
    {
        [0] = {
            .spi = NRFX_SPIM_INSTANCE(0),
            .sck = NRF_GPIO_PIN_MAP(0, 13),
            .mosi = NRF_GPIO_PIN_MAP(1, 15),
            .oe = NRF_GPIO_PIN_MAP(0, 2),
            .row = 0,
            .xfer_desc = NRFX_SPIM_XFER_TRX(hw_NeoPixel.row[0].buf, sizeof(hw_NeoPixel.row[0].buf), NULL, 0)
        },
        [1] = {
            .spi = NRFX_SPIM_INSTANCE(1),
            .sck = NRF_GPIO_PIN_MAP(0, 15),
            .mosi = NRF_GPIO_PIN_MAP(0, 29),
            .oe = NRF_GPIO_PIN_MAP(0, 31),
            .row = 1,
            .xfer_desc = NRFX_SPIM_XFER_TRX(hw_NeoPixel.row[1].buf, sizeof(hw_NeoPixel.row[1].buf), NULL, 0)
        },
        [2] = {
            .spi = NRFX_SPIM_INSTANCE(2),
            .sck = NRF_GPIO_PIN_MAP(0, 17),
            .mosi = NRF_GPIO_PIN_MAP(0, 22),
            .oe = NRF_GPIO_PIN_MAP(0, 20),
            .row = 2,
            .xfer_desc = NRFX_SPIM_XFER_TRX(hw_NeoPixel.row[2].buf, sizeof(hw_NeoPixel.row[2].buf), NULL, 0)
        },
        [3] = {
            .spi = NRFX_SPIM_INSTANCE(3),
            .sck = NRF_GPIO_PIN_MAP(1, 13),
            .mosi = NRF_GPIO_PIN_MAP(1, 0),
            .oe = NRF_GPIO_PIN_MAP(0, 24),
            .row = 3,
            .xfer_desc = NRFX_SPIM_XFER_TRX(hw_NeoPixel.row[3].buf, sizeof(hw_NeoPixel.row[3].buf), NULL, 0)
        }
    }
};

led_ctlr_hw_t* led_ctlr_create(led_ctlr_mode_t mode)
{
    switch(mode)
    {
    case led_ctlr_NeoPixel:
        return &hw_NeoPixel.hw;
    
    case led_ctlr_DotStar:
    default:
        break;
    }
    return NULL;
}

static void np_event_handler(nrfx_spim_evt_t const * p_event, void * p_context)
{
    hw_np_row * r = (hw_np_row *)p_context;
//    struct hw_NeoPixel * np = CONTAINER_OF(r, struct hw_NeoPixel, row[r->row]);

    r->active = false;
    nrf_gpio_pin_set(r->oe);
}

static int np_init(led_ctlr_hw_t* hw)
{
    struct hw_NeoPixel * np = CONTAINER_OF(hw, struct hw_NeoPixel, hw);

    for (int row = 0; row < 4; row++)
    {
        hw_np_row * r = &np->row[row];

        nrfx_spim_config_t spi_config = NRFX_SPIM_DEFAULT_CONFIG;
    
        spi_config.frequency = NRF_SPIM_FREQ_4M;
        spi_config.mosi_pin = r->mosi;
        spi_config.sck_pin = r->sck;
    
        APP_ERROR_CHECK(nrfx_spim_init(&r->spi, &spi_config, np_event_handler, r));

        nrf_gpio_cfg_output(r->oe);
        nrf_gpio_pin_set(r->oe);

        r->active = false;
    }

    return 0;
}


int np_clear(led_ctlr_hw_t* hw)
{
//    struct hw_NeoPixel * np = CONTAINER_OF(hw, struct hw_NeoPixel, hw);

    return 0;
}

int np_show(led_ctlr_hw_t* hw, uint8_t row, uint32_t* buf, uint8_t len)
{
    struct hw_NeoPixel * np = CONTAINER_OF(hw, struct hw_NeoPixel, hw);
    hw_np_row * r = &np->row[row];
    uint8_t* p = r->buf;
    size_t l = 1;
    *p++ = 0;
    for (int i = 0; i < len; i++)
    {
        int ll = np_enc24(buf[i], p);
        l += ll;
        p += ll;
    }
    p[l++] = 0;
    r->xfer_desc.tx_length = l;

    nrf_gpio_pin_clear(r->oe);
    r->active = true;
    APP_ERROR_CHECK(nrfx_spim_xfer(&r->spi, &r->xfer_desc, 0));

    return 0;
}

#endif
