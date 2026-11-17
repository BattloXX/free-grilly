#include "OffDrain.h"

OffDrainResult OffDrain::compute(uint16_t capacity_off_mah, uint16_t capacity_now_mah,
                                 int64_t seconds_off, bool charging_now) {
    OffDrainResult result{false, 0, int32_t(capacity_off_mah) - capacity_now_mah, -1};
    // Preserve elapsed time for diagnostics even when the measurement is invalid.
    if (seconds_off >= 0 && seconds_off <= UINT32_MAX) {
        result.off_seconds = static_cast<uint32_t>(seconds_off);
    }
    if (seconds_off < 3600 || seconds_off > 400LL * 24 * 3600 ||
        charging_now || result.capacity_drop_mah < 0) {
        return result;
    }
    const int64_t numerator = int64_t(result.capacity_drop_mah) * 1000 * 3600;
    result.drain_ua = static_cast<int32_t>((numerator + seconds_off / 2) / seconds_off);
    result.valid = true;
    return result;
}
