#include <pebble.h>
#include <math.h>

// Pebble Round 2: 260x260 circular display
#define SCREEN_W 260
#define SCREEN_H 260
#define CX 130
#define CY 130

// Face
#define FACE_COLOR GColorYellow

// Eye positions — symmetric around CX=130
#define LEFT_EYE_X  95
#define LEFT_EYE_Y  104
#define RIGHT_EYE_X 165
#define RIGHT_EYE_Y 104
#define EYE_R       22
#define PUPIL_R     11

// Mouth — moved up slightly to give room for time badge
#define MOUTH_Y     160
#define MOUTH_W     64
#define MOUTH_H     20

// Time badge — black pill drawn in face layer
#define TIME_BADGE_X  (CX - 68)
#define TIME_BADGE_Y  212
#define TIME_BADGE_W  136
#define TIME_BADGE_H  38
#define TIME_BADGE_R  19   // corner radius = height/2 = perfect pill

// Time text sits inside the badge
#define TIME_Y        215

// --- Animation timing ---
#define FRAME_MS      33      // ~30fps continuous loop — the face is always alive
#define BLINK_MS      170     // duration of a single blink close+open
#define PI_F          3.14159265f

// Battery saver kicks in at/below this %, unless charging. It freezes the
// continuous animation loop (the big power draw) and shows a still face.
#define BATT_SAVER_PCT 20

// Date sits between the mouth and the time badge
#define DATE_Y        189
// Battery % sits near the top of the circle
#define BATT_Y        24

// Mouth shapes
typedef enum {
  MOUTH_SMILE = 0,
  MOUTH_BIG,
  MOUTH_O,
  MOUTH_FLAT,
  MOUTH_SMIRK_L,
  MOUTH_SMIRK_R
} MouthShape;

typedef enum {
  ANIM_NORMAL = 0,
  ANIM_BLINK,
  ANIM_WINK_LEFT,
  ANIM_WINK_RIGHT,
  ANIM_ANGRY,
  ANIM_SURPRISED,
  ANIM_SIDE_EYE_LEFT,
  ANIM_SIDE_EYE_RIGHT,
  ANIM_SLEEPY,
  ANIM_HAPPY,
  ANIM_LOOK_UP,
  ANIM_SMIRK,
  ANIM_COUNT
} AnimState;

// A full facial pose. Everything here is interpolated every frame so the
// face glides between expressions instead of snapping — this is what gives
// the Sphere-like fluid, expressive quality.
typedef struct {
  float lid_l, lid_r;    // eyelid openness: 0 closed .. 1 open (>1 = wide)
  float gaze_x, gaze_y;  // pupil offset in px (where the eyes are looking)
  float pupil;           // pupil scale: 1.0 normal, >1 dilated, <1 constricted
  float brow;            // brow angle: -1 angry .. 0 flat .. +1 worried/raised
  float raise;           // brow vertical raise in "units"
  float mouth_amt;       // eased mouth intensity 0..1 (drives arc/opening size)
  int   mouth;           // discrete mouth shape
} FacePose;

static Window *s_window;
static Layer *s_face_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_batt_layer;

static AppTimer *s_frame_timer = NULL;

// Interpolation state
static FacePose s_cur;   // what's on screen right now
static FacePose s_tgt;   // what we're easing toward

static AnimState s_current_anim = ANIM_NORMAL;

// Scheduling (all in ms, counted down by the frame loop)
static int  s_expr_hold_ms = 0;    // >0 while a triggered expression is held
static bool s_idle = true;         // true when free to wander + auto-blink

// Blink engine (independent of expressions so it stays crisp)
static bool s_blink_active = false;
static int  s_blink_t = 0;         // elapsed ms within current blink
static int  s_blink_side = 0;      // 0=both, 1=left only, 2=right only
static int  s_next_blink_ms = 3000;

// Idle gaze wander
static int  s_next_saccade_ms = 1500;

// Startup expression cycle
static bool s_cycling = false;
static int  s_cycle_index = 0;

// Power saver: true when animations are frozen to conserve battery
static bool s_power_saver = false;

// --- Small math helpers ---

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Exponential smoothing — frame-rate flavoured easing. Higher rate = snappier.
static float ease(float cur, float tgt, float rate) {
  return cur + (tgt - cur) * rate;
}

static int rand_range(int lo, int hi) {
  return lo + (rand() % (hi - lo + 1));
}

// --- Drawing helpers ---

// Draws one eye with a smoothly interpolated eyelid. lid: 0 closed..1 open
// (values >1 open extra-wide for surprise). The lids sweep in from top and
// bottom so blinks and squints read as motion, not a hard cut.
static void draw_eye(GContext *ctx, int cx, int cy, float lid,
                     float gaze_x, float gaze_y, float pupil_scale) {
  // Sclera
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(cx, cy), EYE_R);

  // Outline
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_circle(ctx, GPoint(cx, cy), EYE_R);

  // Pupil — clamp its offset so it never leaves the sclera
  int pr = (int)(PUPIL_R * pupil_scale + 0.5f);
  if (pr < 5) pr = 5;
  float max_off = (float)(EYE_R - pr - 1);
  float gx = clampf(gaze_x, -max_off, max_off);
  float gy = clampf(gaze_y, -max_off, max_off);
  int ppx = cx + (int)(gx + (gx < 0 ? -0.5f : 0.5f));
  int ppy = cy + (int)(gy + (gy < 0 ? -0.5f : 0.5f));

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(ppx, ppy), pr);

  // Glossy catch-light — keeps the eye looking wet/alive like the Sphere
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(ppx + pr / 2, ppy - pr / 2), pr / 3 + 1);

  // Eyelids: cover top (more) and bottom (less) with face colour.
  float closed = 1.0f - clampf(lid, 0.0f, 1.0f);   // 0 open .. 1 shut
  int cover = (int)(closed * (EYE_R + 3));
  if (cover > 0) {
    graphics_context_set_fill_color(ctx, FACE_COLOR);
    int top = (int)(cover * 1.25f);
    int bot = (int)(cover * 0.75f);
    graphics_fill_rect(ctx,
      GRect(cx - EYE_R - 2, cy - EYE_R - 3, (EYE_R + 2) * 2, top + 3),
      0, GCornerNone);
    graphics_fill_rect(ctx,
      GRect(cx - EYE_R - 2, cy + EYE_R - bot, (EYE_R + 2) * 2, bot + 4),
      0, GCornerNone);
  }

  // Nearly shut → draw a soft closed-eye lash line
  if (lid < 0.08f) {
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 4);
    graphics_draw_line(ctx, GPoint(cx - EYE_R + 2, cy), GPoint(cx + EYE_R - 2, cy));
  }
}

// angle: -1 angry .. +1 worried/raised. raise: vertical lift.
static void draw_brow(GContext *ctx, int cx, int cy, float angle, float raise) {
  if (fabsf(angle) < 0.06f && raise < 0.06f) {
    // Resting flat bar
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(cx - 18, cy - 36, 36, 9), 3, GCornersAll);
    return;
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 8);

  int base_y = cy - 34 - (int)(raise * 9.0f);
  bool is_left = (cx < CX);

  // Inner end tilts with angle: angry pushes inner ends DOWN toward the nose,
  // worried lifts them UP.
  int inner_dy = (int)(-angle * 9.0f);
  int outer_dy = (int)(angle * 4.0f);

  int inner_x = is_left ? (cx + 12) : (cx - 12);
  int outer_x = is_left ? (cx - 20) : (cx + 20);

  graphics_draw_line(ctx,
    GPoint(outer_x, base_y + outer_dy),
    GPoint(inner_x, base_y + inner_dy));
}

static void draw_mouth(GContext *ctx, MouthShape shape, float amt) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 4);

  switch (shape) {
    case MOUTH_O: {
      int r = (int)(6 + 9 * amt);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, GPoint(CX, MOUTH_Y + 6), r);
      break;
    }
    case MOUTH_BIG: {
      int r = 24;
      GRect mr = GRect(CX - r, MOUTH_Y - r, r * 2, r * 2);
      graphics_draw_arc(ctx, mr, GOvalScaleModeFitCircle,
                        DEG_TO_TRIGANGLE(125), DEG_TO_TRIGANGLE(235));
      break;
    }
    case MOUTH_FLAT:
      graphics_draw_line(ctx,
        GPoint(CX - MOUTH_W / 2 + 6, MOUTH_Y + 6),
        GPoint(CX + MOUTH_W / 2 - 6, MOUTH_Y + 6));
      break;
    case MOUTH_SMIRK_L:
    case MOUTH_SMIRK_R: {
      int side = (shape == MOUTH_SMIRK_R) ? 1 : -1;
      int cx = CX + side * 10;
      int r  = 14;
      GRect mr = GRect(cx - r, MOUTH_Y - r, r * 2, r * 2);
      graphics_draw_arc(ctx, mr, GOvalScaleModeFitCircle,
                        DEG_TO_TRIGANGLE(148), DEG_TO_TRIGANGLE(218));
      break;
    }
    case MOUTH_SMILE:
    default: {
      int r = 18;
      GRect mr = GRect(CX - r, MOUTH_Y - r, r * 2, r * 2);
      graphics_draw_arc(ctx, mr, GOvalScaleModeFitCircle,
                        DEG_TO_TRIGANGLE(148), DEG_TO_TRIGANGLE(212));
      break;
    }
  }
}

// --- Face layer update ---

static void face_layer_update(Layer *layer, GContext *ctx) {
  // Fill full rect — round display hardware clips to circle
  graphics_context_set_fill_color(ctx, FACE_COLOR);
  graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, SCREEN_H), 0, GCornerNone);

  // Apply the live blink multiplier on top of the eased lid values.
  float blink = 1.0f;
  if (s_blink_active) {
    float p = (float)s_blink_t / (float)BLINK_MS;      // 0..1
    blink = 1.0f - sinf(clampf(p, 0.0f, 1.0f) * PI_F); // dips to 0 mid-blink
  }
  float lid_l = s_cur.lid_l * (s_blink_side == 2 ? 1.0f : blink);
  float lid_r = s_cur.lid_r * (s_blink_side == 1 ? 1.0f : blink);

  draw_brow(ctx, LEFT_EYE_X,  LEFT_EYE_Y,  -s_cur.brow, s_cur.raise);
  draw_brow(ctx, RIGHT_EYE_X, RIGHT_EYE_Y,  s_cur.brow, s_cur.raise);

  draw_eye(ctx, LEFT_EYE_X,  LEFT_EYE_Y,  lid_l,
           s_cur.gaze_x, s_cur.gaze_y, s_cur.pupil);
  draw_eye(ctx, RIGHT_EYE_X, RIGHT_EYE_Y, lid_r,
           s_cur.gaze_x, s_cur.gaze_y, s_cur.pupil);

  draw_mouth(ctx, (MouthShape)s_cur.mouth, s_cur.mouth_amt);

  // Black pill time badge — circular display mask clips the outer edges
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx,
    GRect(TIME_BADGE_X, TIME_BADGE_Y, TIME_BADGE_W, TIME_BADGE_H),
    TIME_BADGE_R, GCornersAll);
}

// --- Expression targets ---

// Resets the target to the neutral resting pose.
static void pose_neutral(FacePose *p) {
  p->lid_l = p->lid_r = 1.0f;
  p->gaze_x = p->gaze_y = 0.0f;
  p->pupil = 1.0f;
  p->brow = 0.0f;
  p->raise = 0.0f;
  p->mouth_amt = 1.0f;
  p->mouth = MOUTH_SMILE;
}

// Sets s_tgt (and any one-shot blink) for the given expression.
static void set_expression(AnimState s) {
  pose_neutral(&s_tgt);
  s_blink_side = 0;

  switch (s) {
    case ANIM_BLINK:
      // Just a blink over the neutral face
      break;
    case ANIM_WINK_LEFT:
      s_blink_side = 1;
      s_tgt.mouth = MOUTH_SMIRK_R;
      s_tgt.gaze_x = 3;
      break;
    case ANIM_WINK_RIGHT:
      s_blink_side = 2;
      s_tgt.mouth = MOUTH_SMIRK_L;
      s_tgt.gaze_x = -3;
      break;
    case ANIM_ANGRY:
      s_tgt.brow = -1.0f;
      s_tgt.pupil = 0.85f;
      s_tgt.lid_l = s_tgt.lid_r = 0.8f;
      s_tgt.mouth = MOUTH_FLAT;
      break;
    case ANIM_SURPRISED:
      s_tgt.lid_l = s_tgt.lid_r = 1.25f;
      s_tgt.raise = 1.0f;
      s_tgt.pupil = 1.45f;
      s_tgt.gaze_y = -2;
      s_tgt.mouth = MOUTH_O;
      break;
    case ANIM_SIDE_EYE_LEFT:
      s_tgt.gaze_x = -9;
      s_tgt.lid_l = s_tgt.lid_r = 0.85f;
      s_tgt.mouth = MOUTH_SMIRK_R;
      break;
    case ANIM_SIDE_EYE_RIGHT:
      s_tgt.gaze_x = 9;
      s_tgt.lid_l = s_tgt.lid_r = 0.85f;
      s_tgt.mouth = MOUTH_SMIRK_L;
      break;
    case ANIM_SLEEPY:
      s_tgt.lid_l = s_tgt.lid_r = 0.32f;
      s_tgt.gaze_y = 3;
      s_tgt.pupil = 0.9f;
      s_tgt.mouth = MOUTH_FLAT;
      break;
    case ANIM_HAPPY:
      s_tgt.gaze_y = -2;
      s_tgt.pupil = 1.15f;
      s_tgt.mouth = MOUTH_BIG;
      break;
    case ANIM_LOOK_UP:
      s_tgt.gaze_y = -10;
      s_tgt.raise = 1.0f;
      break;
    case ANIM_SMIRK:
      s_tgt.gaze_x = 5;
      s_tgt.mouth = MOUTH_SMIRK_R;
      break;
    default:
      break;
  }
}

// Kicks off a blink (both eyes unless an expression narrowed s_blink_side).
static void start_blink(int side) {
  s_blink_active = true;
  s_blink_t = 0;
  s_blink_side = side;
}

// --- Scheduling / frame loop ---

static void trigger_expression(AnimState s, int hold_ms) {
  s_current_anim = s;
  set_expression(s);
  s_expr_hold_ms = hold_ms;
  s_idle = false;

  // Expressions that are fundamentally a blink fire one immediately.
  if (s == ANIM_BLINK || s == ANIM_WINK_LEFT || s == ANIM_WINK_RIGHT) {
    start_blink(s_blink_side);
  }
}

static void return_to_idle(void) {
  s_current_anim = ANIM_NORMAL;
  pose_neutral(&s_tgt);
  s_idle = true;
  s_next_saccade_ms = rand_range(1200, 2800);
}

static void advance_cycle(void) {
  s_cycle_index++;
  if (s_cycle_index < ANIM_COUNT) {
    trigger_expression((AnimState)s_cycle_index, 700);
  } else {
    s_cycling = false;
    return_to_idle();
  }
}

static void frame_timer_callback(void *data) {
  const int dt = FRAME_MS;

  // --- Advance the blink ---
  if (s_blink_active) {
    s_blink_t += dt;
    if (s_blink_t >= BLINK_MS) {
      s_blink_active = false;
      s_blink_side = (s_current_anim == ANIM_WINK_LEFT) ? 1
                   : (s_current_anim == ANIM_WINK_RIGHT) ? 2 : 0;
    }
  }

  // --- Expression hold countdown ---
  if (s_expr_hold_ms > 0) {
    s_expr_hold_ms -= dt;
    if (s_expr_hold_ms <= 0) {
      if (s_cycling) {
        advance_cycle();
      } else {
        return_to_idle();
      }
    }
  }

  // --- Idle life: wandering gaze + natural blinks ---
  if (s_idle) {
    s_next_saccade_ms -= dt;
    if (s_next_saccade_ms <= 0) {
      // Mostly small glances, occasionally a bigger look — like the Sphere
      // eye idly scanning the room.
      if (rand() % 100 < 15) {
        s_tgt.gaze_x = rand_range(-9, 9);
        s_tgt.gaze_y = rand_range(-6, 5);
      } else {
        s_tgt.gaze_x = rand_range(-5, 5);
        s_tgt.gaze_y = rand_range(-3, 3);
      }
      s_next_saccade_ms = rand_range(1200, 2800);
    }
  }

  // Natural blinks happen in idle regardless of gaze.
  if (s_idle && !s_blink_active) {
    s_next_blink_ms -= dt;
    if (s_next_blink_ms <= 0) {
      start_blink(0);
      s_next_blink_ms = rand_range(2600, 6000);
    }
  }

  // --- Ease every parameter toward the target ---
  s_cur.gaze_x    = ease(s_cur.gaze_x,    s_tgt.gaze_x,    0.16f);
  s_cur.gaze_y    = ease(s_cur.gaze_y,    s_tgt.gaze_y,    0.16f);
  s_cur.lid_l     = ease(s_cur.lid_l,     s_tgt.lid_l,     0.30f);
  s_cur.lid_r     = ease(s_cur.lid_r,     s_tgt.lid_r,     0.30f);
  s_cur.pupil     = ease(s_cur.pupil,     s_tgt.pupil,     0.18f);
  s_cur.brow      = ease(s_cur.brow,      s_tgt.brow,      0.22f);
  s_cur.raise     = ease(s_cur.raise,     s_tgt.raise,     0.22f);
  s_cur.mouth_amt = ease(s_cur.mouth_amt, s_tgt.mouth_amt, 0.25f);

  // Mouth shape is discrete — swap it once we've eased close to a rest point
  // so the change lands during motion rather than as a hard pop.
  s_cur.mouth = s_tgt.mouth;

  layer_mark_dirty(s_face_layer);

  // Keep the loop alive only while animations are enabled.
  s_frame_timer = s_power_saver
                ? NULL
                : app_timer_register(FRAME_MS, frame_timer_callback, NULL);
}

static void trigger_random_anim(void) {
  s_cycling = false;
  trigger_expression((AnimState)(1 + (rand() % (ANIM_COUNT - 1))), 2500);
}

static void start_cycle(void) {
  s_cycling = true;
  s_cycle_index = 1;  // start at ANIM_BLINK (skip ANIM_NORMAL)
  trigger_expression((AnimState)s_cycle_index, 700);
}

// --- Power saver ---

// Freezes/thaws the animation loop. In saver mode the face rests on a still
// neutral pose and the 30fps timer stops entirely, so the only work left is
// the once-a-minute clock tick.
static void set_power_saver(bool on) {
  if (on == s_power_saver) return;
  s_power_saver = on;

  if (on) {
    if (s_frame_timer) {
      app_timer_cancel(s_frame_timer);
      s_frame_timer = NULL;
    }
    s_cycling = false;
    s_blink_active = false;
    s_expr_hold_ms = 0;
    s_idle = true;
    pose_neutral(&s_cur);
    pose_neutral(&s_tgt);
    layer_mark_dirty(s_face_layer);
  } else if (!s_frame_timer) {
    s_frame_timer = app_timer_register(FRAME_MS, frame_timer_callback, NULL);
  }
}

static void battery_handler(BatteryChargeState state) {
  static char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", state.charge_percent);
  text_layer_set_text(s_batt_layer, buf);

  bool low = (state.charge_percent <= BATT_SAVER_PCT)
             && !state.is_charging && !state.is_plugged;
  set_power_saver(low);
}

// --- Time update ---

static void update_time(struct tm *tick_time) {
  static char buf[8];
  if (clock_is_24h_style()) {
    strftime(buf, sizeof(buf), "%H:%M", tick_time);
  } else {
    strftime(buf, sizeof(buf), "%I:%M", tick_time);
  }
  text_layer_set_text(s_time_layer, buf);
}

static void update_date(struct tm *tick_time) {
  static char buf[16];
  strftime(buf, sizeof(buf), "%a %d %b", tick_time);   // e.g. "Wed 01 Jul"
  text_layer_set_text(s_date_layer, buf);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  if (units_changed & DAY_UNIT) {
    update_date(tick_time);
  }
  // Don't spend battery on random expressions while saving power.
  if ((units_changed & MINUTE_UNIT) && !s_power_saver) {
    trigger_random_anim();
  }
}

// --- Window setup ---

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);

  s_face_layer = layer_create(GRect(0, 0, SCREEN_W, SCREEN_H));
  layer_set_update_proc(s_face_layer, face_layer_update);
  layer_add_child(root, s_face_layer);

  // Time text — white, centred inside the black pill badge
  s_time_layer = text_layer_create(GRect(CX - 58, TIME_Y, 116, 32));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer,
    fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  // Date — black text on the face, above the time badge
  s_date_layer = text_layer_create(GRect(CX - 60, DATE_Y, 120, 22));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorBlack);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_date_layer));

  // Battery % — small, near the top of the circle
  s_batt_layer = text_layer_create(GRect(CX - 40, BATT_Y, 80, 22));
  text_layer_set_background_color(s_batt_layer, GColorClear);
  text_layer_set_text_color(s_batt_layer, GColorBlack);
  text_layer_set_font(s_batt_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_batt_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_batt_layer));

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  update_time(t);
  update_date(t);

  // Start both poses at neutral so the first ease-in is clean.
  pose_neutral(&s_cur);
  pose_neutral(&s_tgt);

  tick_timer_service_subscribe(MINUTE_UNIT | DAY_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  srand(time(NULL));

  // Seed battery %/saver state from the current charge immediately.
  battery_handler(battery_state_service_peek());

  // Only spin up the animation loop if we're not already saving power.
  if (!s_power_saver) {
    start_cycle();
    s_frame_timer = app_timer_register(FRAME_MS, frame_timer_callback, NULL);
  }
}

static void window_unload(Window *window) {
  if (s_frame_timer) {
    app_timer_cancel(s_frame_timer);
    s_frame_timer = NULL;
  }
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  layer_destroy(s_face_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_batt_layer);
}

// --- Main ---

int main(void) {
  s_window = window_create();
  window_set_background_color(s_window, FACE_COLOR);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  app_event_loop();
  window_destroy(s_window);
  return 0;
}
