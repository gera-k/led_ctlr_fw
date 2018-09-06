#ifndef LED_CTLR_H
#define LED_CTLR_H

#include "led_ctlr_hw.h"

int led_ctlr_init(led_ctlr_mode_t mode);

void led_ctlr_start();

#endif /* LED_CTLR_H */