#include <pebble.h>
#include "anim.h"
#include "bg.h"
#include "face.h"
#include "fixed.h"
#include "raster.h"
#include "settings.h"
#include "weather.h"

// Orbi: an expressive orb face for the Pebble Round 2 (gabbro).
//
// Frames only run while something is moving. Between shows the timer stops
// and the last frame stays on screen, so a still face costs nothing but the
// minute tick. How often he moves is the "Liveliness" setting.

#define FRAME_MS 33
#define AWAKE_S 8               // idle glances and blinks after a show
#define RAISE_WINDOW_MS 1500
#define RAISE_COOLDOWN_MS 3000
#define WEATHER_ASK_S (5 * 60)  // don't ask the phone more often than this

static Window *s_window;
static Layer *s_face_layer;
static TextLayer *s_time_layer, *s_date_layer, *s_batt_layer;
static AppTimer *s_timer;
static bool s_running;
static uint64_t s_last_ms;
static time_t s_awake_until, s_last_poke, s_last_ask;
static uint8_t *s_rows[SCREEN_H];
static int16_t s_minx[SCREEN_H], s_maxx[SCREEN_H];
static GBitmap *s_bitmaps[PROP_COUNT];
static BatteryChargeState s_batt;
static bool s_bt = true, s_saver, s_quiet, s_charging;
static int s_since_act;
static uint8_t s_last_wx = WX_UNKNOWN;
static bool s_raise_on;
static uint64_t s_away_ts, s_raise_ts;
static uint8_t s_view_run;
static char s_time_buf[8], s_date_buf[28], s_batt_buf[8];
#ifdef ORBI_DEBUG
static int s_dbg_ms;
static char s_dbg_buf[24];
#endif

static uint64_t now_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (uint64_t)s * 1000 + ms;
}

static int live_level(void) {
#ifdef ORBI_FREEZE
  return LIVE_STILL;
#endif
  if (s_saver) return LIVE_STILL;
  if (s_quiet && g_settings.live > LIVE_CALM) return LIVE_CALM;
  return g_settings.live;
}

static Mood base_mood(void) {
  if (s_quiet) return MOOD_ASLEEP;
  if (s_saver) return MOOD_TIRED;
  if (!s_bt && g_settings.bt_alert) return MOOD_WORRIED;
  return MOOD_NORMAL;
}

// ------------------------------------------------------------ frame loop ----

static void frame(void *data);

static void idle_wake(void *data) {
  s_timer = NULL;
  if (rand() % 100 < 25 && !s_quiet) {
    anim_play(ACT_GLANCE);
  } else {
    anim_blink();
  }
  s_last_ms = now_ms();
  s_running = true;
  s_timer = app_timer_register(FRAME_MS, frame, NULL);
}

static void schedule(void) {
  int lvl = live_level();
  bool awake = lvl == LIVE_ALWAYS || (lvl >= LIVE_CALM && time(NULL) < s_awake_until);
  anim_set_idle(awake);
  anim_set_ambient(awake && lvl >= LIVE_LIVELY);
#if defined(ORBI_LOOP)
  if (!anim_playing()) anim_play(ORBI_LOOP);
  awake = true;
#elif defined(ORBI_DEMO)
  // The store GIF: a fixed run of shows, back to back.
  static const ActId DEMO[] = { ACT_LAUGH, ACT_COOL, ACT_LOVE };
  static unsigned demo;
  if (!anim_playing()) anim_play(DEMO[demo++ % ARRAY_LENGTH(DEMO)]);
  awake = true;
#endif
  if (anim_busy() || awake) {
    s_running = true;
    s_timer = app_timer_register(FRAME_MS, frame, NULL);
    return;
  }
  s_running = false;
  // A blink every so often keeps him alive between shows.
  if (lvl >= LIVE_CALM) s_timer = app_timer_register(rand_range(6000, 16000), idle_wake, NULL);
}

static void frame(void *data) {
  s_timer = NULL;
  uint64_t t = now_ms();
  // time_ms() can jump; never let one long gap fling the springs.
  float dt = (float)(t - s_last_ms) / 1000.0f;
  if (dt <= 0 || dt > 0.1f) dt = FRAME_MS / 1000.0f;
  s_last_ms = t;
  anim_step(dt);
  layer_mark_dirty(s_face_layer);
  schedule();
}

// Starts frames now if they aren't running.
static void wake_frames(void) {
  if (s_running) return;
  if (s_timer) app_timer_cancel(s_timer);
  s_last_ms = now_ms();
  s_running = true;
  s_timer = app_timer_register(FRAME_MS, frame, NULL);
}

// When nothing may move, jump straight to the resting pose and redraw.
static void refresh(void) {
  if (live_level() == LIVE_STILL) {
    anim_settle();
    if (s_face_layer) layer_mark_dirty(s_face_layer);
  } else {
    wake_frames();
  }
}

static void play(ActId act) {
  if (live_level() == LIVE_STILL || act == ACT_NONE) return;
  anim_play(act);
  s_awake_until = time(NULL) + AWAKE_S;
  wake_frames();
}

// ----------------------------------------------------------------- props ----

static uint32_t prop_resource(int id) {
  switch (id) {
    case PROP_SUNGLASSES: return RESOURCE_ID_PROP_SUNGLASSES;
    case PROP_BEANIE: return RESOURCE_ID_PROP_BEANIE;
    case PROP_UMBRELLA: return RESOURCE_ID_PROP_UMBRELLA;
    case PROP_PARTYHAT: return RESOURCE_ID_PROP_PARTYHAT;
    case PROP_WITCHHAT: return RESOURCE_ID_PROP_WITCHHAT;
    case PROP_SANTAHAT: return RESOURCE_ID_PROP_SANTAHAT;
    case PROP_NIGHTCAP: return RESOURCE_ID_PROP_NIGHTCAP;
    default: return RESOURCE_ID_PROP_CLOUD;
  }
}

// Bitmaps load when a prop appears and are freed when it has gone, so only
// what is on screen costs RAM.
static void draw_props(GContext *ctx, const FaceState *f) {
  bool used[PROP_COUNT] = { false };
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  for (int slot = 0; slot < SLOT_COUNT; slot++) {
    int id = f->props[slot].id;
    if (id <= PROP_NONE || id >= PROP_COUNT) continue;
    used[id] = true;
    if (!s_bitmaps[id]) s_bitmaps[id] = gbitmap_create_with_resource(prop_resource(id));
    if (!s_bitmaps[id]) continue;
    GSize sz = gbitmap_get_bounds(s_bitmaps[id]).size;
    int x, y;
    if (face_prop_pos(f, slot, sz.w, sz.h, &x, &y)) {
      graphics_draw_bitmap_in_rect(ctx, s_bitmaps[id], GRect(x, y, sz.w, sz.h));
    }
  }
  for (int id = 1; id < PROP_COUNT; id++) {
    if (!used[id] && s_bitmaps[id]) {
      gbitmap_destroy(s_bitmaps[id]);
      s_bitmaps[id] = NULL;
    }
  }
}

// ----------------------------------------------------------------- draw ----

static void rows_from(GBitmap *fb) {
  for (int y = 0; y < SCREEN_H; y++) {
    GBitmapDataRowInfo r = gbitmap_get_data_row_info(fb, y);
    s_rows[y] = r.data;
    s_minx[y] = r.min_x;
    s_maxx[y] = r.max_x < SCREEN_W - 1 ? r.max_x : SCREEN_W - 1;
  }
}

static void face_update(Layer *layer, GContext *ctx) {
#ifdef ORBI_DEBUG
  uint64_t t0 = now_ms();
#endif
  const FaceState *f = anim_face();
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  rows_from(fb);
  face_render_back(f, s_rows, s_minx, s_maxx);
  graphics_release_frame_buffer(ctx, fb);
  draw_props(ctx, f);
  fb = graphics_capture_frame_buffer(ctx);
  if (fb) {
    rows_from(fb);
    face_render_front(f, s_rows, s_minx, s_maxx);
    graphics_release_frame_buffer(ctx, fb);
  }
  // A hat or the umbrella sits where the battery reading is.
  bool covered = (f->props[SLOT_HEAD].id && f->props[SLOT_HEAD].show > 0.3f) ||
                 (f->props[SLOT_SKY].id && f->props[SLOT_SKY].show > 0.3f);
  bool batt = g_settings.battery_style == BATT_PERCENT ||
              (g_settings.battery_style == BATT_LOW_ONLY && (s_batt.charge_percent <= 30 || s_charging));
  layer_set_hidden(text_layer_get_layer(s_batt_layer), covered || !batt);
#ifdef ORBI_DEBUG
  s_dbg_ms = (int)(now_ms() - t0);
  snprintf(s_dbg_buf, sizeof(s_dbg_buf), "%dms %s", s_dbg_ms, anim_busy() ? "run" : "idle");
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_dbg_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(80, 44, 100, 16),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
#endif
}

// ----------------------------------------------------------------- text ----

static void update_time(struct tm *t) {
  int h = t->tm_hour, m = t->tm_min;
#ifdef ORBI_SHOW_TIME
  h = ORBI_SHOW_TIME / 100;
  m = ORBI_SHOW_TIME % 100;
#endif
  bool h24 = g_settings.clock_format == CLOCK_24H ||
             (g_settings.clock_format == CLOCK_WATCH && clock_is_24h_style());
  if (!h24) {
    h %= 12;
    if (h == 0) h = 12;
  }
  snprintf(s_time_buf, sizeof(s_time_buf), g_settings.leading_zero ? "%02d:%02d" : "%d:%02d", h, m);
  text_layer_set_text(s_time_layer, s_time_buf);
}

static void update_date(struct tm *t) {
  static const char *const FORMATS[] = { "", "%a %d %b", "%a %d", "%d %b", "%b %d", "%d/%m", "%m/%d" };
  char d[16] = "";
  if (g_settings.date_format < ARRAY_LENGTH(FORMATS)) {
    strftime(d, sizeof(d), FORMATS[g_settings.date_format], t);
  }
  const Weather *wx = weather_now();
  if (g_settings.show_temp && g_settings.weather && wx->kind != WX_UNKNOWN) {
    int temp = g_settings.fahrenheit ? wx->temp_c * 9 / 5 + 32 : wx->temp_c;
    snprintf(s_date_buf, sizeof(s_date_buf), d[0] ? "%s  %d°" : "%s%d°", d, temp);
  } else {
    snprintf(s_date_buf, sizeof(s_date_buf), "%s", d);
  }
  text_layer_set_text(s_date_layer, s_date_buf);
  layer_set_hidden(text_layer_get_layer(s_date_layer), s_date_buf[0] == 0);
}

static void update_battery_text(void) {
  int pct = s_batt.charge_percent;
#ifdef ORBI_BATT
  pct = ORBI_BATT;
#endif
  snprintf(s_batt_buf, sizeof(s_batt_buf), s_charging ? "+%d%%" : "%d%%", pct);
  text_layer_set_text(s_batt_layer, s_batt_buf);
}

static void update_all_text(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  update_time(t);
  update_date(t);
  update_battery_text();
}

// -------------------------------------------------------------- settings ----

static void apply_settings(void) {
  int theme = g_settings.theme;
#ifdef ORBI_THEME
  theme = ORBI_THEME;
#endif
  bg_build(theme);
  bool pill = g_settings.clock_style == CLOCKSTYLE_PILL;
  face_set_pill(pill);
  text_layer_set_text_color(s_time_layer, pill ? GColorWhite : GColorBlack);
  text_layer_set_font(s_time_layer, fonts_get_system_font(pill ? FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM
                                                               : FONT_KEY_LECO_28_LIGHT_NUMBERS));
  layer_set_hidden(text_layer_get_layer(s_time_layer), g_settings.clock_style == CLOCKSTYLE_HIDDEN);
  anim_set_props(g_settings.props);
  anim_set_weather(weather_now(), g_settings.weather);
  anim_set_mood(base_mood());
  update_all_text();
}

// --------------------------------------------------------------- events ----

static bool in_quiet(const struct tm *t) {
  if (!g_settings.quiet) return false;
  int h = t->tm_hour, a = g_settings.quiet_start, b = g_settings.quiet_end;
  return a <= b ? (h >= a && h < b) : (h >= a || h < b);
}

static void update_quiet(const struct tm *t) {
  bool q = in_quiet(t);
  if (q == s_quiet) return;
  s_quiet = q;
  anim_set_mood(base_mood());
  if (q) {
    play(ACT_SLEEP);
  } else {
    anim_clear_props();
    play(ACT_WAKE);
  }
  refresh();
}

static void tick(struct tm *t, TimeUnits changed) {
  update_time(t);
  if (changed & DAY_UNIT) update_date(t);
  update_quiet(t);
  if (g_settings.weather && weather_due() && time(NULL) - s_last_ask > WEATHER_ASK_S) {
    s_last_ask = time(NULL);
    weather_request();
  }
  if (live_level() < LIVE_LIVELY || s_quiet) return;
#ifdef ORBI_DEMO
  return;   // the demo runs its own fixed sequence
#endif
  if (t->tm_min == 0 && g_settings.hourly) {
    play(ACT_PARTY);
    s_since_act = 0;
    return;
  }
  if (++s_since_act >= g_settings.act_every) {
    s_since_act = 0;
    play(anim_pick(weather_now(), t->tm_mon, t->tm_mday, g_settings.weather, g_settings.seasonal,
                   g_settings.props));
  }
}

static void battery(BatteryChargeState st) {
  bool was_charging = s_charging;
  s_batt = st;
  s_charging = st.is_charging || st.is_plugged;
  int pct = st.charge_percent;
#ifdef ORBI_BATT
  pct = ORBI_BATT;
#endif
  bool saver = g_settings.saver_pct && pct <= g_settings.saver_pct && !s_charging;
  if (saver != s_saver) {
    s_saver = saver;
    anim_set_mood(base_mood());
    if (saver) {
      anim_clear_props();
    } else {
      play(ACT_GLANCE);
    }
    refresh();
  }
  if (s_charging && !was_charging && s_window) play(ACT_CHARGING);
  update_battery_text();
}

static void bluetooth(bool connected) {
  if (connected == s_bt) return;
  s_bt = connected;
  anim_set_mood(base_mood());
  if (!connected && g_settings.bt_alert) {
    vibes_double_pulse();
    play(ACT_BT_LOST);
  } else if (connected) {
    play(ACT_GLANCE);
  }
  refresh();
}

static void poke(ActId act) {
  if (live_level() == LIVE_STILL) return;
  time_t now = time(NULL);
  if (s_quiet) {
    act = ACT_WAKE;
  } else if (now - s_last_poke <= 2) {
    act = ACT_DIZZY;   // shaken again straight away
  }
  s_last_poke = now;
  play(act);
}

static void flick(AccelAxisType axis, int32_t direction) {
  if (!g_settings.wake_flick) return;
  static const ActId REACTIONS[] = { ACT_POKED, ACT_POKED, ACT_SURPRISED, ACT_WINK, ACT_CHEEKY };
  poke(REACTIONS[rand() % ARRAY_LENGTH(REACTIONS)]);
}

static void touch(const TouchEvent *event, void *context) {
  if (event->type != TouchEvent_Touchdown || !g_settings.wake_tap) return;
  // Poke him in the eye and he flinches; anywhere else tickles.
  poke(event->y < 140 && event->y > 60 ? ACT_SURPRISED : ACT_LAUGH);
}

// Pebble has no wrist-raise event, so this reads the accelerometer: the
// screen swinging from sideways or down to facing up within a short window.
static void accel_data(AccelData *d, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    if (d[i].did_vibrate) continue;
    int x = d[i].x, z = d[i].z;
    uint64_t ts = d[i].timestamp;
    if (z > -200) {
      s_away_ts = ts;
      s_view_run = 0;
      continue;
    }
    if (z > -550 || x < -700 || x > 700) {
      s_view_run = 0;
      continue;
    }
    if (s_view_run < 255) s_view_run++;
    if (s_view_run == 2 && s_away_ts && ts - s_away_ts < RAISE_WINDOW_MS &&
        ts - s_raise_ts > RAISE_COOLDOWN_MS) {
      s_raise_ts = ts;
      play(s_quiet ? ACT_NONE : (rand() & 1) ? ACT_GLANCE : ACT_WINK);
    }
  }
}

static void update_raise(void) {
  if (g_settings.wake_raise && !s_raise_on) {
    accel_data_service_subscribe(5, accel_data);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
    s_raise_on = true;
  } else if (!g_settings.wake_raise && s_raise_on) {
    accel_data_service_unsubscribe();
    s_raise_on = false;
  }
}

static void inbox(DictionaryIterator *it, void *context) {
  bool wx = weather_handle_inbox(it);
  bool st = settings_handle_inbox(it);
  if (st) {
    apply_settings();
    update_raise();
  }
  if (wx) {
    const Weather *w = weather_now();
    anim_set_weather(w, g_settings.weather);
    update_all_text();
    // React when the weather changes, not every time it is re-sent.
    if (g_settings.weather && w->kind != s_last_wx && s_last_wx != WX_UNKNOWN && !s_quiet &&
        live_level() >= LIVE_LIVELY) {
      play(anim_weather_act(w));
    }
    s_last_wx = w->kind;
  }
  if (st || wx) refresh();
}

// --------------------------------------------------------------- window ----

static TextLayer *make_text(GRect r, const char *font) {
  TextLayer *t = text_layer_create(r);
  text_layer_set_background_color(t, GColorClear);
  text_layer_set_text_color(t, GColorBlack);
  text_layer_set_font(t, fonts_get_system_font(font));
  text_layer_set_text_alignment(t, GTextAlignmentCenter);
  return t;
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_face_layer = layer_create(GRect(0, 0, SCREEN_W, SCREEN_H));
  layer_set_update_proc(s_face_layer, face_update);
  layer_add_child(root, s_face_layer);

  s_time_layer = make_text(GRect(PILL_X, PILL_Y - 1, PILL_W, 32), FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
  s_date_layer = make_text(GRect(CX - 70, 186, 140, 24), FONT_KEY_GOTHIC_18_BOLD);
  s_batt_layer = make_text(GRect(CX - 40, 16, 80, 24), FONT_KEY_GOTHIC_18_BOLD);
  layer_add_child(root, text_layer_get_layer(s_time_layer));
  layer_add_child(root, text_layer_get_layer(s_date_layer));
  layer_add_child(root, text_layer_get_layer(s_batt_layer));

  s_batt = battery_state_service_peek();
  s_charging = s_batt.is_charging || s_batt.is_plugged;
  s_bt = connection_service_peek_pebble_app_connection();
  time_t now = time(NULL);
  s_quiet = in_quiet(localtime(&now));
  s_last_wx = weather_now()->kind;
  apply_settings();
  battery(s_batt);

  tick_timer_service_subscribe(MINUTE_UNIT | DAY_UNIT, tick);
  battery_state_service_subscribe(battery);
  connection_service_subscribe((ConnectionHandlers){ .pebble_app_connection_handler = bluetooth });
  accel_tap_service_subscribe(flick);
  touch_service_subscribe(touch, NULL);
  update_raise();

#if defined(ORBI_FREEZE)
  anim_freeze(ORBI_FREEZE, ORBI_FREEZE_MS);
#elif defined(ORBI_DEMO)
  srand(5);
  wake_frames();
#else
  if (s_quiet) {
    anim_play(ACT_SLEEP);
    anim_settle();
  }
  // Say hello.
  play(s_quiet ? ACT_NONE : ACT_WINK);
  refresh();
#endif
}

static void window_unload(Window *window) {
  if (s_timer) app_timer_cancel(s_timer);
  s_timer = NULL;
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  accel_tap_service_unsubscribe();
  touch_service_unsubscribe();
  if (s_raise_on) accel_data_service_unsubscribe();
  for (int i = 0; i < PROP_COUNT; i++) {
    if (s_bitmaps[i]) gbitmap_destroy(s_bitmaps[i]);
    s_bitmaps[i] = NULL;
  }
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_batt_layer);
  layer_destroy(s_face_layer);
  bg_free();
}

int main(void) {
  srand(time(NULL));
  settings_load();
  weather_load();
  anim_init();
  app_message_register_inbox_received(inbox);
  app_message_open(512, 64);
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
  app_event_loop();
  window_destroy(s_window);
  return 0;
}
