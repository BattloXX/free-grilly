#pragma once

#include <stddef.h>
#include <stdint.h>

// A cook starts when the first history sample appears after all histories were empty. Plain C++ so
// test/test_cook_session runs on the host: pio test -e native
namespace history {

class CookSession {
public:
    void set_salt(uint32_t salt) { salt_ = salt; }
    bool update(bool any_history_before, bool any_history_after);
    bool active() const { return counter_ > 0; }
    void format_id(char* buffer, size_t size) const;

private:
    uint32_t salt_ = 0;
    uint16_t counter_ = 0;
};

}
