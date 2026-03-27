#include "LEDController.h"
#include <Arduino.h>
#include "config.h"

// Use channels 5-7 to avoid conflict with camera (uses 0-1 for XCLK)
#define LED_RED_CH   5
#define LED_GREEN_CH 6
#define LED_BLUE_CH  7

void initLED() {
    ledcSetup(LED_RED_CH,   LEDC_FREQ, LEDC_RES);
    ledcSetup(LED_GREEN_CH, LEDC_FREQ, LEDC_RES);
    ledcSetup(LED_BLUE_CH,  LEDC_FREQ, LEDC_RES);
    ledcAttachPin(LED_RED_PIN,   LED_RED_CH);
    ledcAttachPin(LED_GREEN_PIN, LED_GREEN_CH);
    ledcAttachPin(LED_BLUE_PIN,  LED_BLUE_CH);
    ledOff();
    Serial.println("[LED] Init OK (RGB common anode, LEDC ch 5/6/7)");
}

// Common anode: invert duty so 255 = full on, 0 = full off
void setColor(uint8_t r, uint8_t g, uint8_t b) {
    ledcWrite(LED_RED_CH,   255 - r);
    ledcWrite(LED_GREEN_CH, 255 - g);
    ledcWrite(LED_BLUE_CH,  255 - b);
    Serial.printf("[LED] setColor R=%u G=%u B=%u\n", r, g, b);
}

void ledOn()  { setColor(255, 255, 255); Serial.println("[LED] ON (white)"); }
void ledOff() { setColor(0, 0, 0);       Serial.println("[LED] OFF"); }
