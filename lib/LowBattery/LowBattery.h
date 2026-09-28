#pragma once
#include <stdint.h>

// Decides when the battery is so empty that the grill should switch itself off, before the voltage
// sags into brownouts and reboot loops. Plain C++ without Arduino, so test/test_low_battery runs on
// the host: pio test -e native
//
// Low means: not charging, and 5 % or less (unless the voltage is clearly fine, which points at a
// confused fuel gauge) or 3.2 V or less. It has to be low on 15 good readings in a row (one per
// second), so a single odd reading never switches the grill off. Failed readings don't count either way.
class LowBattery {
public:
    static constexpr int      MAX_PERCENTAGE      = 5;
    static constexpr int      MAX_MILLIVOLTS      = 3200;
    static constexpr int      HEALTHY_MILLIVOLTS  = 3600;   // above this a low percentage is not trusted
    static constexpr int      READINGS_TO_CONFIRM = 15;

    // Call once per battery reading. read_ok is false when the fuel gauge didn't answer.
    // millivolts is 0 when unknown. Returns true when the grill should switch off now.
    bool update(bool read_ok, int percentage, int millivolts, bool charging);

    int low_readings() const { return low_readings_; }

private:
    int low_readings_ = 0;
};
