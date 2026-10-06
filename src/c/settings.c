#include "settings.h"

#define SETTINGS_KEY 1
#define SETTINGS_VERSION 1

Settings g_settings;

// Stored with a version byte so a later layout change can't misread old bytes.
typedef struct {
  uint8_t version;
  Settings s;
} Stored;

static void set_defaults(void) {
  g_settings = (Settings){
    .clock_format = CLOCK_WATCH,
    .leading_zero = true,
    .clock_style = CLOCKSTYLE_PILL,
    .date_format = DATE_DAY_NUM_MON,
    .show_temp = true,
    .battery_style = BATT_PERCENT,
    .live = LIVE_LIVELY,
    .act_every = 2,
    .saver_pct = 20,
    .quiet = false,
    .quiet_start = 23,
    .quiet_end = 7,
    .wake_flick = true,
    .wake_tap = true,
    .wake_raise = false,
    .theme = 0,
    .props = true,
    .seasonal = true,
    .hourly = true,
    .weather = true,
    .fahrenheit = false,
    .bt_alert = true,
  };
}

static void save(void) {
  Stored st = { SETTINGS_VERSION, g_settings };
  persist_write_data(SETTINGS_KEY, &st, sizeof(st));
}

void settings_load(void) {
  set_defaults();
  Stored st;
  if (persist_read_data(SETTINGS_KEY, &st, sizeof(st)) == sizeof(st) &&
      st.version == SETTINGS_VERSION) {
    g_settings = st.s;
  }
}

// Clay sends selects as strings and toggles as integers; accept either so a
// Clay version change can't silently break a setting.
static bool read_int(DictionaryIterator *it, uint32_t key, int32_t *out) {
  Tuple *t = dict_find(it, key);
  if (!t) return false;
  if (t->type == TUPLE_CSTRING) {
    *out = atoi(t->value->cstring);
  } else if (t->length == 1) {
    *out = t->type == TUPLE_INT ? t->value->int8 : t->value->uint8;
  } else if (t->length == 2) {
    *out = t->type == TUPLE_INT ? t->value->int16 : t->value->uint16;
  } else {
    *out = t->value->int32;
  }
  return true;
}

static bool read_u8(DictionaryIterator *it, uint32_t key, uint8_t *dst, int lo, int hi) {
  int32_t v;
  if (!read_int(it, key, &v)) return false;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  *dst = (uint8_t)v;
  return true;
}

static bool read_bool(DictionaryIterator *it, uint32_t key, bool *dst) {
  int32_t v;
  if (!read_int(it, key, &v)) return false;
  *dst = v != 0;
  return true;
}

bool settings_handle_inbox(DictionaryIterator *it) {
  bool any = false;
  any |= read_u8(it, MESSAGE_KEY_ClockFormat, &g_settings.clock_format, 0, 2);
  any |= read_bool(it, MESSAGE_KEY_LeadingZero, &g_settings.leading_zero);
  any |= read_u8(it, MESSAGE_KEY_ClockStyle, &g_settings.clock_style, 0, 2);
  any |= read_u8(it, MESSAGE_KEY_DateFormat, &g_settings.date_format, 0, 6);
  any |= read_bool(it, MESSAGE_KEY_ShowTemp, &g_settings.show_temp);
  any |= read_u8(it, MESSAGE_KEY_BatteryStyle, &g_settings.battery_style, 0, 2);
  any |= read_u8(it, MESSAGE_KEY_Live, &g_settings.live, 0, 3);
  any |= read_u8(it, MESSAGE_KEY_ActEvery, &g_settings.act_every, 1, 30);
  any |= read_u8(it, MESSAGE_KEY_SaverPct, &g_settings.saver_pct, 0, 50);
  any |= read_bool(it, MESSAGE_KEY_Quiet, &g_settings.quiet);
  any |= read_u8(it, MESSAGE_KEY_QuietStart, &g_settings.quiet_start, 0, 23);
  any |= read_u8(it, MESSAGE_KEY_QuietEnd, &g_settings.quiet_end, 0, 23);
  any |= read_bool(it, MESSAGE_KEY_WakeFlick, &g_settings.wake_flick);
  any |= read_bool(it, MESSAGE_KEY_WakeTap, &g_settings.wake_tap);
  any |= read_bool(it, MESSAGE_KEY_WakeRaise, &g_settings.wake_raise);
  any |= read_u8(it, MESSAGE_KEY_Theme, &g_settings.theme, 0, 4);
  any |= read_bool(it, MESSAGE_KEY_Props, &g_settings.props);
  any |= read_bool(it, MESSAGE_KEY_Seasonal, &g_settings.seasonal);
  any |= read_bool(it, MESSAGE_KEY_Hourly, &g_settings.hourly);
  any |= read_bool(it, MESSAGE_KEY_Weather, &g_settings.weather);
  any |= read_bool(it, MESSAGE_KEY_Fahrenheit, &g_settings.fahrenheit);
  any |= read_bool(it, MESSAGE_KEY_BtAlert, &g_settings.bt_alert);
  if (any) save();
  return any;
}
