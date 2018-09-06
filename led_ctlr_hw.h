#ifndef LED_CTLR_HW_H
#define LED_CTLR_HW_H

#include "led_show.h"

typedef enum led_ctlr_mode
{
    led_ctlr_NeoPixel = (1 << 0),
    led_ctlr_DotStar  = (1 << 1),
} led_ctlr_mode_t; 

// led_ctlr_hw - controller HW interface
//  includes SPIs, row select GPIOs, external 3.3-5 drivers
//  this does not include features of the LED set attached to the controller
//  a controller may support multiple types of LED sets 
typedef struct led_ctlr_hw led_ctlr_hw_t;
struct led_ctlr_hw
{
    uint8_t mode;               // bitmask of led_ctlr_mode_t this controller supports
    uint8_t rows;               // max number of rows this controller supports
    uint8_t rows_per_refresh;   // number of rows the hw can update in one shot

    int (*init)(led_ctlr_hw_t* hw);     // initialize hardware

    int (*clear)(led_ctlr_hw_t* hw);    // clear (turn off) all LEDs in all rows
    
    int (*show)(led_ctlr_hw_t* hw,      // show content of buffer
        uint8_t row,                    // row number
        uint32_t* buf,                  // buffer containing row data, 00RRGGBB
        uint8_t len                     // buffer length (in uint32_t)
    );
};

// create HW controller for given LED mode
//  if external HW is requird, the function assumes that correct hw is already attached
led_ctlr_hw_t* led_ctlr_create(led_ctlr_mode_t mode);

#endif /* LED_CTLR_HW_H */