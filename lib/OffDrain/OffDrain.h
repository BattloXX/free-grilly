#pragma once
#include <stdint.h>

struct OffDrainResult {
    bool valid;
    uint32_t off_seconds;
    int32_t capacity_drop_mah;
    int32_t drain_ua;
};

class OffDrain {
public:
    static OffDrainResult compute(uint16_t capacity_off_mah, uint16_t capacity_now_mah,
                                  int64_t seconds_off, bool charging_now);
};
