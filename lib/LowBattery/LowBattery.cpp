#include "LowBattery.h"

bool LowBattery::update(bool read_ok, int percentage, int millivolts, bool charging){
    if(!read_ok){ return false; }

    bool voltage_known = millivolts > 0;
    bool low_voltage   = voltage_known && millivolts <= MAX_MILLIVOLTS;
    bool low_charge    = percentage <= MAX_PERCENTAGE && (!voltage_known || millivolts < HEALTHY_MILLIVOLTS);

    if(charging || !(low_voltage || low_charge)){
        low_readings_ = 0;
        return false;
    }

    low_readings_++;
    return low_readings_ >= READINGS_TO_CONFIRM;
}
