// Keep this file self-contained: the wake stub runs before flash is available,
// so it must use only RTC code/data, direct registers and ROM functions.
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "rom/ets_sys.h"
#include "rom/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc.h"

#if CONFIG_ESP32_RTCDATA_IN_FAST_MEM
#error "The wake-stub counter must live in RTC slow memory"
#endif

// RTC_DATA_ATTR maps to slow memory in this configuration. Never modify RTC fast
// memory here: the ROM checks its CRC before entering the stub on the next wake.
static RTC_DATA_ATTR volatile uint32_t wake_stub_ignored = 0;

extern "C" void RTC_IRAM_ATTR esp_wake_deep_sleep(void) {
    bool pressed = true;
    for (unsigned sample = 0; sample < 10; ++sample) {
        // GPIO35 is RTC_GPIO5; the input-only pad has no internal pull.
        if ((REG_READ(RTC_GPIO_IN_REG) >> (RTC_GPIO_IN_NEXT_S + 5)) & 1) {
            pressed = false;
        }
        ets_delay_us(2000);
    }

    if (pressed) {
        esp_default_wake_deep_sleep();
        return;
    }

    ++wake_stub_ignored;
    // EXT0 and the individual RTC pad holds remain configured from shutdown().
    REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&esp_wake_deep_sleep);
    CLEAR_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    SET_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    while (true) {}
}

uint32_t wake_stub_take_ignored() {
    const uint32_t count = wake_stub_ignored;
    wake_stub_ignored = 0;
    return count;
}
