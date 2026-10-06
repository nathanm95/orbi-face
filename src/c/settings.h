#pragma once
#include <pebble.h>

// Everything on the settings page (src/pkjs/config.js), persisted on the watch.

typedef enum { CLOCK_WATCH = 0, CLOCK_12H, CLOCK_24H } ClockFormat;
typedef enum { CLOCKSTYLE_PILL = 0, CLOCKSTYLE_PLAIN, CLOCKSTYLE_HIDDEN } ClockStyle;
typedef enum {
  DATE_HIDDEN = 0,
  DATE_DAY_NUM_MON,   // Tue 06 Oct
  DATE_DAY_NUM,       // Tue 06
  DATE_NUM_MON,       // 06 Oct
  DATE_MON_NUM,       // Oct 06
  DATE_DMY,           // 06/10
  DATE_MDY,           // 10/06
} DateFormat;
typedef enum { BATT_HIDDEN = 0, BATT_PERCENT, BATT_LOW_ONLY } BatteryStyle;
// How much Orbi moves, which is most of the battery cost.
typedef enum {
  LIVE_STILL = 0,    // never animates
  LIVE_CALM,         // only when you flick or tap; blinks now and then
  LIVE_LIVELY,       // plus a little show every few minutes (default)
  LIVE_ALWAYS,       // never stops moving: heavy on the battery
} LiveLevel;

typedef struct {
  uint8_t clock_format;
  bool leading_zero;
  uint8_t clock_style;
  uint8_t date_format;
  bool show_temp;
  uint8_t battery_style;

  uint8_t live;           // LiveLevel
  uint8_t act_every;      // minutes between shows when lively
  uint8_t saver_pct;      // battery saver threshold, 0 = off
  bool quiet;             // sleeps through quiet hours
  uint8_t quiet_start, quiet_end;   // hours
  bool wake_flick;
  bool wake_tap;
  bool wake_raise;

  uint8_t theme;
  bool props;
  bool seasonal;
  bool hourly;
  bool weather;
  bool fahrenheit;
  bool bt_alert;
} Settings;

extern Settings g_settings;

void settings_load(void);
// Applies an AppMessage from the settings page. Returns true if anything changed.
bool settings_handle_inbox(DictionaryIterator *iter);
