#pragma once
#include <pebble.h>
#include "anim.h"

// The current weather, sent by the phone (src/pkjs/index.js fetches it from
// Open-Meteo). Kept in persistent storage so a restart doesn't lose it, and
// ignored once it is three hours old.

void weather_load(void);
// The weather if it is fresh, else kind WX_UNKNOWN.
const Weather *weather_now(void);
// Reads a weather AppMessage. Returns true if it carried weather.
bool weather_handle_inbox(DictionaryIterator *iter);
// Asks the phone for an update.
void weather_request(void);
// True if the cached report is old enough to ask for a new one.
bool weather_due(void);
