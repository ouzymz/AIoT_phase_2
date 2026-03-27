#include "UltrasonicSensor.h"
#include <Arduino.h>
#include "config.h"

void initUltrasonic() {
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);
    Serial.println("[Ultrasonic] Init OK");
}

float getDistance() {
    // Send 10us pulse
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // Measure echo pulse (timeout 30ms)
    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    if (duration == 0) {
        Serial.println("[Ultrasonic] Timeout (no echo)");
        return -1.0f;
    }
    const float TEMP_CELSIUS = 20.0f;
    const float soundSpeed_cm_us = (331.45f + (0.62f * TEMP_CELSIUS)) / 10000.0f;// 343.85 m/s → 0.034385 cm/µs
    float distance = (duration * soundSpeed_cm_us) / 2.0f;
    
    Serial.printf("[Ultrasonic] Distance: %.2f cm\n", distance);
    return distance;
}
