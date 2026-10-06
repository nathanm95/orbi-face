#include "weather.h"

#define WEATHER_KEY 2
#define STALE_S (3 * 60 * 60)
#define REFRESH_S (30 * 60)

typedef struct {
  Weather wx;
  int32_t at;   // time() when it arrived
} Stored;

static Stored s_stored;
static Weather s_unknown = { WX_UNKNOWN, 0, true, 0 };

void weather_load(void) {
  if (persist_read_data(WEATHER_KEY, &s_stored, sizeof(s_stored)) != sizeof(s_stored)) {
    s_stored.at = 0;
    s_stored.wx = s_unknown;
  }
}

const Weather *weather_now(void) {
#ifdef ORBI_WEATHER
  static Weather forced = { ORBI_WEATHER, ORBI_TEMP, true, ORBI_WIND };
  return &forced;
#endif
  if (s_stored.at == 0 || time(NULL) - s_stored.at > STALE_S) return &s_unknown;
  return &s_stored.wx;
}

bool weather_due(void) { return s_stored.at == 0 || time(NULL) - s_stored.at > REFRESH_S; }

bool weather_handle_inbox(DictionaryIterator *iter) {
  Tuple *code = dict_find(iter, MESSAGE_KEY_WxCode);
  Tuple *temp = dict_find(iter, MESSAGE_KEY_WxTemp);
  if (!code || !temp) return false;
  Tuple *day = dict_find(iter, MESSAGE_KEY_WxDay);
  Tuple *wind = dict_find(iter, MESSAGE_KEY_WxWind);
  s_stored.wx.kind = (uint8_t)code->value->int32;
  s_stored.wx.temp_c = (int8_t)temp->value->int32;
  s_stored.wx.is_day = day ? day->value->int32 != 0 : true;
  s_stored.wx.wind_kmh = wind ? (uint8_t)wind->value->int32 : 0;
  s_stored.at = (int32_t)time(NULL);
  persist_write_data(WEATHER_KEY, &s_stored, sizeof(s_stored));
  return true;
}

void weather_request(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_int8(out, MESSAGE_KEY_WxRequest, 1);
  app_message_outbox_send();
}
