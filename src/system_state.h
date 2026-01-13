#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>

struct SystemState {
    // Battery Data (Saftkiste)
    float batt_voltage = -1.0;
    float batt_current = -1.0;
    float batt_soc = -1.0;
    float batt_power = -1.0;

    // PV Data
    float pv1_voltage = -1.0;
    float pv2_voltage = -1.0;
    float pv_power = -1.0;
    
    // Consumption / Grid
    float verbrauchsleistung = -1.0;
    float einspeisung_heute_Wh = -1.0;

    // Weather
    float temperature = -99.0;

    // Wireless Charging
    int w_chg_cnt = 0;

    // Animation / Visuals
    int sonnenStrahlStatus = 0;
    int target_brightness = 255; // Soll-Helligkeit
};

extern SystemState sysState;

#endif
