#include <stdio.h>
#include "CookSession.h"

namespace history {

bool CookSession::update(bool any_history_before, bool any_history_after){
    if(any_history_before || !any_history_after){ return false; }

    counter_++;
    if(counter_ == 0){ counter_ = 1; }
    return true;
}

void CookSession::format_id(char* buffer, size_t size) const {
    snprintf(buffer, size, "c-%08lx-%04u", (unsigned long)salt_, (unsigned int)counter_);
}

}
