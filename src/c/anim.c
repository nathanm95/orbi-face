#include "anim.h"
#include "fixed.h"
#include "raster.h"

// ------------------------------------------------------------- channels ----
// Every continuous part of the face is a damped spring. Under-damped springs
// overshoot a little, which is most of what makes the face feel alive: a head
// turn that settles, eyes that pop, a bounce that wobbles.

enum {
  CH_YAW, CH_PITCH, CH_ROLL, CH_BOB, CH_SCALE,
  CH_GAZE_X, CH_GAZE_Y, CH_CROSS, CH_PUPIL, CH_EYE, CH_EYE_L, CH_EYE_R,
  CH_LT_L, CH_LT_R, CH_LB_L, CH_LB_R, CH_LID_TILT, CH_HAPPY,
  CH_BROW_Y, CH_BROW_TILT, CH_BROW_L, CH_BROW_R, CH_BROW_ARCH,
  CH_M_OPEN, CH_M_W, CH_M_CURVE, CH_M_ROUND, CH_M_SKEW, CH_M_X, CH_TEETH, CH_TONGUE,
  CH_CHEEK, CH_FLUSH, CH_SWEAT,
  CH_COUNT,
  // Pseudo channels for expression tables: both eyes at once.
  X_LT = 100, X_LB,
};

// Channels measured in px rather than 0..1, for the settle test.
#define PX_CHANNEL(c) ((c) == CH_BOB || (c) == CH_BROW_Y || (c) == CH_BROW_L || \
                       (c) == CH_BROW_R || (c) == CH_M_X)

typedef struct {
  float x, v, t;
} Spring;

static FaceState s_face;
static Spring s_sp[CH_COUNT];
static float s_neutral[CH_COUNT];
static float s_k[CH_COUNT], s_d[CH_COUNT];
static float s_style = 1;   // stiffness multiplier for the current act phase

static void set_gains(void) {
  for (int c = 0; c < CH_COUNT; c++) {
    s_k[c] = 280;
    s_d[c] = 24;
  }
  for (int c = CH_YAW; c <= CH_ROLL; c++) {
    s_k[c] = 70;
    s_d[c] = 9;
  }
  s_k[CH_BOB] = 220, s_d[CH_BOB] = 9;
  s_k[CH_SCALE] = 260, s_d[CH_SCALE] = 14;
  s_k[CH_GAZE_X] = s_k[CH_GAZE_Y] = 300, s_d[CH_GAZE_X] = s_d[CH_GAZE_Y] = 27;
  s_k[CH_EYE] = 220, s_d[CH_EYE] = 13;
  for (int c = CH_LT_L; c <= CH_LB_R; c++) {
    s_k[c] = 520;
    s_d[c] = 44;
  }
  s_k[CH_BROW_Y] = 260, s_d[CH_BROW_Y] = 18;
  for (int c = CH_CHEEK; c <= CH_SWEAT; c++) {
    s_k[c] = 30;
    s_d[c] = 11;
  }
}

// ----------------------------------------------------------- expressions ----

typedef struct { uint8_t ch; int16_t v; } EV;   // v = value * 100
#define END_EV { 255, 0 }

enum {
  E_NEUTRAL, E_HAPPY, E_GRIN, E_LAUGH, E_SURPRISED, E_SHOCKED, E_ANGRY, E_SIGH, E_SAD, E_CRY,
  E_SNIFF, E_SLEEPY, E_ASLEEP, E_YAWN, E_SMIRK, E_SUSPICIOUS, E_LOVE, E_KISS, E_CHEEKY, E_COOL,
  E_SCARED, E_MEH, E_DIZZY, E_COLD, E_HOT, E_WORRIED, E_TIRED, E_WHISTLE, E_SNEEZE_PRE,
  E_SNEEZE, E_SILLY, E_HIC, E_EMBARRASSED, E_WONDER, E_TONGUE_UP, E_SMUG, E_SQUINT, E_SHRUG,
  E_WINCE, E_EVIL, E_BLISS, E_GROGGY, E_CURIOUS, E_COUNT
};

static const EV X_NEUTRAL[] = { END_EV };
static const EV X_HAPPY[] = { { CH_M_CURVE, 95 }, { CH_M_OPEN, 22 }, { CH_M_W, 108 }, { CH_TEETH, 50 },
  { CH_HAPPY, 55 }, { X_LB, 32 }, { CH_CHEEK, 55 }, { CH_BROW_Y, 300 }, { CH_BROW_ARCH, 60 }, END_EV };
static const EV X_GRIN[] = { { CH_M_OPEN, 45 }, { CH_TEETH, 100 }, { CH_M_CURVE, 100 }, { CH_M_W, 120 },
  { CH_CHEEK, 50 }, { CH_HAPPY, 45 }, { X_LB, 28 }, { CH_BROW_Y, 300 }, END_EV };
static const EV X_LAUGH[] = { { CH_M_OPEN, 95 }, { CH_M_CURVE, 100 }, { CH_TEETH, 100 }, { CH_HAPPY, 100 },
  { X_LT, 100 }, { X_LB, 100 }, { CH_BROW_Y, 700 }, { CH_BROW_ARCH, 90 }, { CH_CHEEK, 90 },
  { CH_M_W, 115 }, END_EV };
static const EV X_SURPRISED[] = { { CH_EYE, 118 }, { CH_PUPIL, 72 }, { CH_BROW_Y, 1200 },
  { CH_BROW_ARCH, 110 }, { CH_M_OPEN, 62 }, { CH_M_ROUND, 100 }, { CH_M_W, 70 }, END_EV };
static const EV X_SHOCKED[] = { { CH_EYE, 130 }, { CH_PUPIL, 50 }, { CH_BROW_Y, 1600 },
  { CH_BROW_ARCH, 120 }, { CH_M_OPEN, 95 }, { CH_M_ROUND, 85 }, { CH_M_W, 85 }, END_EV };
static const EV X_ANGRY[] = { { CH_LID_TILT, 100 }, { X_LT, 40 }, { CH_BROW_TILT, 100 },
  { CH_BROW_Y, -500 }, { CH_BROW_ARCH, -30 }, { CH_M_CURVE, -60 }, { CH_M_W, 80 }, { CH_PUPIL, 85 },
  { CH_FLUSH, 55 }, END_EV };
static const EV X_SIGH[] = { { X_LT, 45 }, { CH_M_OPEN, 14 }, { CH_M_ROUND, 100 }, { CH_M_W, 40 },
  { CH_BROW_TILT, -30 }, { CH_M_CURVE, 0 }, END_EV };
static const EV X_SAD[] = { { CH_LID_TILT, -80 }, { X_LT, 28 }, { CH_BROW_TILT, -100 }, { CH_BROW_Y, 400 },
  { CH_M_CURVE, -70 }, { CH_M_W, 80 }, { CH_GAZE_Y, 35 }, { CH_PUPIL, 115 }, END_EV };
static const EV X_CRY[] = { { CH_LID_TILT, -90 }, { X_LT, 45 }, { X_LB, 25 }, { CH_BROW_TILT, -120 },
  { CH_BROW_Y, 600 }, { CH_M_OPEN, 40 }, { CH_M_CURVE, -90 }, { CH_M_ROUND, 30 }, { CH_M_W, 90 },
  { CH_PUPIL, 120 }, END_EV };
static const EV X_SNIFF[] = { { X_LT, 30 }, { CH_M_CURVE, 10 }, { CH_M_W, 50 }, { CH_BROW_TILT, -40 },
  { CH_PITCH, -6 }, END_EV };
static const EV X_SLEEPY[] = { { X_LT, 62 }, { X_LB, 10 }, { CH_BROW_Y, -200 }, { CH_M_CURVE, 15 },
  { CH_GAZE_Y, 30 }, { CH_PUPIL, 95 }, END_EV };
static const EV X_ASLEEP[] = { { X_LT, 100 }, { X_LB, 100 }, { CH_HAPPY, -60 }, { CH_M_OPEN, 10 }, { CH_M_ROUND, 100 },
  { CH_M_W, 40 }, { CH_BROW_Y, -200 }, { CH_PITCH, 5 }, END_EV };
static const EV X_YAWN[] = { { CH_M_OPEN, 110 }, { CH_M_ROUND, 75 }, { CH_M_W, 80 }, { X_LT, 85 },
  { X_LB, 40 }, { CH_BROW_Y, 800 }, { CH_BROW_TILT, -40 }, END_EV };
static const EV X_SMIRK[] = { { CH_M_CURVE, 55 }, { CH_M_SKEW, 80 }, { CH_M_X, 600 }, { CH_BROW_R, 600 },
  { X_LT, 25 }, { CH_GAZE_X, 45 }, END_EV };
static const EV X_SUSPICIOUS[] = { { X_LT, 55 }, { X_LB, 25 }, { CH_GAZE_X, -80 }, { CH_BROW_TILT, 50 },
  { CH_BROW_L, -300 }, { CH_BROW_R, 300 }, { CH_M_CURVE, -15 }, { CH_M_W, 60 }, { CH_M_SKEW, -40 }, END_EV };
static const EV X_LOVE[] = { { CH_M_CURVE, 100 }, { CH_M_OPEN, 30 }, { CH_TEETH, 60 }, { CH_CHEEK, 100 },
  { CH_BROW_Y, 400 }, { CH_EYE, 110 }, END_EV };
static const EV X_KISS[] = { { CH_M_OPEN, 18 }, { CH_M_ROUND, 100 }, { CH_M_W, 45 }, { CH_CHEEK, 80 },
  { CH_M_X, 400 }, { CH_BROW_Y, 300 }, END_EV };
static const EV X_CHEEKY[] = { { CH_TONGUE, 100 }, { CH_M_OPEN, 30 }, { CH_M_CURVE, 80 }, { CH_LT_L, 100 }, { CH_LB_L, 60 },
  { CH_HAPPY, 60 }, { CH_CHEEK, 40 }, { CH_BROW_R, 500 }, { CH_M_W, 105 }, END_EV };
static const EV X_COOL[] = { { CH_M_CURVE, 55 }, { CH_M_SKEW, 60 }, { X_LT, 45 }, { X_LB, 30 }, { CH_EYE, 85 },
  { CH_BROW_Y, 200 }, { CH_M_X, 500 }, END_EV };
static const EV X_SCARED[] = { { CH_EYE, 115 }, { CH_PUPIL, 55 }, { CH_BROW_TILT, -100 }, { CH_BROW_Y, 1000 },
  { CH_M_OPEN, 35 }, { CH_M_ROUND, 30 }, { CH_TEETH, 100 }, { CH_M_CURVE, -50 }, { CH_M_W, 110 }, END_EV };
static const EV X_MEH[] = { { X_LT, 45 }, { CH_M_CURVE, -25 }, { CH_M_SKEW, -50 }, { CH_BROW_TILT, 30 },
  { CH_GAZE_X, 50 }, { CH_M_W, 70 }, END_EV };
static const EV X_DIZZY[] = { { CH_M_CURVE, -20 }, { CH_M_SKEW, 60 }, { CH_BROW_TILT, -40 },
  { CH_M_OPEN, 20 }, { CH_M_ROUND, 60 }, END_EV };
static const EV X_COLD[] = { { CH_CHEEK, 90 }, { CH_M_OPEN, 30 }, { CH_TEETH, 100 }, { CH_M_CURVE, -20 },
  { CH_BROW_TILT, -50 }, { X_LT, 20 }, { CH_M_W, 95 }, END_EV };
static const EV X_HOT[] = { { CH_SWEAT, 100 }, { X_LT, 40 }, { CH_M_OPEN, 45 }, { CH_TONGUE, 70 },
  { CH_BROW_TILT, -40 }, { CH_FLUSH, 25 }, { CH_M_CURVE, 20 }, END_EV };
static const EV X_WORRIED[] = { { CH_BROW_TILT, -90 }, { CH_BROW_Y, 400 }, { CH_M_CURVE, -35 },
  { CH_M_SKEW, 30 }, { CH_M_W, 75 }, { CH_PUPIL, 110 }, END_EV };
static const EV X_TIRED[] = { { X_LT, 50 }, { CH_BROW_TILT, -50 }, { CH_M_CURVE, -10 }, { CH_SWEAT, 60 },
  { CH_PUPIL, 90 }, { CH_GAZE_Y, 20 }, END_EV };
static const EV X_WHISTLE[] = { { CH_M_OPEN, 12 }, { CH_M_ROUND, 100 }, { CH_M_W, 38 }, { CH_M_X, 800 },
  { CH_BROW_Y, 400 }, { X_LT, 20 }, { CH_GAZE_Y, -40 }, END_EV };
static const EV X_SNEEZE_PRE[] = { { X_LT, 55 }, { X_LB, 25 }, { CH_BROW_Y, 900 }, { CH_BROW_TILT, -60 },
  { CH_M_OPEN, 45 }, { CH_M_ROUND, 60 }, { CH_PITCH, -8 }, END_EV };
static const EV X_SNEEZE[] = { { X_LT, 100 }, { X_LB, 100 }, { CH_LID_TILT, 70 }, { CH_M_OPEN, 55 },
  { CH_M_ROUND, 20 }, { CH_BROW_TILT, 60 }, { CH_BROW_Y, -300 }, { CH_PITCH, 14 }, { CH_M_W, 110 }, END_EV };
static const EV X_SILLY[] = { { CH_CROSS, 100 }, { CH_TONGUE, 100 }, { CH_M_OPEN, 30 }, { CH_BROW_L, 700 },
  { CH_BROW_R, -200 }, { CH_M_CURVE, 70 }, { CH_EYE_L, 112 }, { CH_EYE_R, 92 }, END_EV };
static const EV X_HIC[] = { { CH_EYE, 112 }, { CH_M_OPEN, 20 }, { CH_M_ROUND, 100 }, { CH_M_W, 40 },
  { CH_BROW_Y, 800 }, END_EV };
static const EV X_EMBARRASSED[] = { { CH_CHEEK, 100 }, { CH_M_CURVE, 30 }, { CH_M_SKEW, 40 },
  { CH_GAZE_X, -60 }, { CH_GAZE_Y, 60 }, { X_LT, 30 }, { CH_M_W, 60 }, END_EV };
static const EV X_WONDER[] = { { CH_EYE, 110 }, { CH_PUPIL, 125 }, { CH_M_OPEN, 22 }, { CH_M_ROUND, 100 },
  { CH_M_W, 50 }, { CH_BROW_Y, 700 }, { CH_BROW_ARCH, 100 }, { CH_GAZE_Y, -80 }, END_EV };
static const EV X_TONGUE_UP[] = { { CH_TONGUE, 100 }, { CH_M_OPEN, 35 }, { CH_M_CURVE, 40 },
  { CH_GAZE_Y, -90 }, { CH_BROW_Y, 600 }, { CH_CHEEK, 70 }, END_EV };
static const EV X_SMUG[] = { { CH_M_CURVE, 70 }, { CH_M_SKEW, 50 }, { CH_HAPPY, 40 }, { X_LB, 30 },
  { X_LT, 30 }, { CH_BROW_Y, 200 }, { CH_CHEEK, 30 }, END_EV };
static const EV X_SQUINT[] = { { X_LT, 52 }, { X_LB, 30 }, { CH_BROW_TILT, 35 }, { CH_M_CURVE, -10 },
  { CH_M_W, 70 }, { CH_SCALE, 104 }, END_EV };
static const EV X_SHRUG[] = { { CH_BROW_Y, 900 }, { CH_BROW_ARCH, 100 }, { CH_M_CURVE, -20 },
  { CH_M_SKEW, -40 }, { CH_M_W, 60 }, END_EV };
static const EV X_WINCE[] = { { X_LT, 60 }, { X_LB, 40 }, { CH_LID_TILT, 50 }, { CH_M_CURVE, -40 },
  { CH_M_W, 90 }, { CH_TEETH, 80 }, { CH_M_OPEN, 15 }, END_EV };
static const EV X_EVIL[] = { { CH_BROW_TILT, 110 }, { CH_BROW_Y, -300 }, { CH_LID_TILT, 70 }, { X_LT, 35 },
  { CH_M_OPEN, 50 }, { CH_TEETH, 100 }, { CH_M_CURVE, 100 }, { CH_M_W, 120 }, END_EV };
static const EV X_BLISS[] = { { X_LT, 100 }, { X_LB, 100 }, { CH_HAPPY, 100 }, { CH_M_CURVE, 100 },
  { CH_M_OPEN, 20 }, { CH_CHEEK, 90 }, { CH_BROW_Y, 500 }, END_EV };
static const EV X_GROGGY[] = { { X_LT, 60 }, { CH_BROW_L, 500 }, { CH_M_CURVE, 0 }, { CH_M_W, 60 },
  { CH_GAZE_Y, 20 }, END_EV };
static const EV X_CURIOUS[] = { { CH_BROW_Y, 400 }, { CH_BROW_R, 300 }, { CH_PUPIL, 110 }, END_EV };

static const EV *const EXPRS[E_COUNT] = {
  X_NEUTRAL, X_HAPPY, X_GRIN, X_LAUGH, X_SURPRISED, X_SHOCKED, X_ANGRY, X_SIGH, X_SAD, X_CRY,
  X_SNIFF, X_SLEEPY, X_ASLEEP, X_YAWN, X_SMIRK, X_SUSPICIOUS, X_LOVE, X_KISS, X_CHEEKY, X_COOL,
  X_SCARED, X_MEH, X_DIZZY, X_COLD, X_HOT, X_WORRIED, X_TIRED, X_WHISTLE, X_SNEEZE_PRE,
  X_SNEEZE, X_SILLY, X_HIC, X_EMBARRASSED, X_WONDER, X_TONGUE_UP, X_SMUG, X_SQUINT, X_SHRUG,
  X_WINCE, X_EVIL, X_BLISS, X_GROGGY, X_CURIOUS,
};

// ------------------------------------------------------------------ acts ----
// An act is a list of timed keys. Values are x100 so they fit in an int16.

enum {
  OP_END, OP_EXPR, OP_SET, OP_SNAP, OP_GAZE, OP_HEAD, OP_BLINK, OP_EYES, OP_PROP, OP_PROPDX,
  OP_EMIT, OP_SHAKE, OP_KICK, OP_FLASH, OP_STYLE, OP_WEATHER, OP_OSC,
};

// Emit sources.
enum { S_MOUTH, S_EYES, S_SIDES, S_ORBIT, S_SKY, S_GLASSES, S_ZZZ, S_LEFT, S_CLOUD, S_TOP, S_TEMPLE,
       S_AROUND };

// a: for GAZE/HEAD/BLINK/SET, MIRROR flips x by a coin toss made per act.
#define MIRROR 1

typedef struct {
  uint16_t t;
  uint8_t op, a;
  int16_t b, c;
} Key;

#define K(t, op, a, b, c) { t, op, a, b, c }
#define EXPR(t, e) K(t, OP_EXPR, e, 0, 0)
#define SET(t, ch, v) K(t, OP_SET, ch, v, 0)
#define GAZE(t, x, y) K(t, OP_GAZE, 0, x, y)
#define GAZEM(t, x, y) K(t, OP_GAZE, MIRROR, x, y)
#define HEAD(t, yaw, pitch) K(t, OP_HEAD, 0, yaw, pitch)
#define HEADM(t, yaw, pitch) K(t, OP_HEAD, MIRROR, yaw, pitch)
#define BLINK(t) K(t, OP_BLINK, 0, 0, 0)
#define WINK(t, side, hold) K(t, OP_BLINK, side, hold, 0)
#define EYES(t, m) K(t, OP_EYES, m, 0, 0)
#define PROP(t, slot, id) K(t, OP_PROP, slot, id, 0)
#define EMIT(t, kind, n, src) K(t, OP_EMIT, kind, n, src)
#define SHAKE(t, amp10, ms) K(t, OP_SHAKE, 0, amp10, ms)
#define KICK(t, ch, v) K(t, OP_KICK, ch, v, 0)
#define FLASH(t) K(t, OP_FLASH, 0, 0, 0)
#define STYLE(t, s) K(t, OP_STYLE, s, 0, 0)
#define WEATHER(t, kind, rate10) K(t, OP_WEATHER, kind, rate10, 0)
#define OSC(t, ch, amp, ms) K(t, OP_OSC, ch, amp, ms)
#define FIN(t) K(t, OP_END, 0, 0, 0)

enum { ST_NORMAL, ST_SLOW, ST_SNAPPY };

static const Key A_GLANCE[] = {
  GAZEM(0, 85, -20), HEADM(0, 16, -4), SET(100, CH_BROW_Y, 400),
  GAZE(900, 0, 0), HEAD(900, 0, 0), SET(900, CH_BROW_Y, 0), FIN(1500),
};
static const Key A_LOOK_AROUND[] = {
  EXPR(0, E_SQUINT), GAZEM(0, -90, 0), HEADM(0, -28, 0),
  GAZEM(1100, 90, 0), HEADM(1100, 28, 0),
  GAZE(2300, 0, 0), HEAD(2300, 0, 0), EXPR(2300, E_SHRUG), BLINK(2400),
  EXPR(3000, E_NEUTRAL), FIN(3400),
};
static const Key A_LAUGH[] = {
  EXPR(0, E_GRIN), KICK(0, CH_BOB, -9000), EXPR(220, E_LAUGH),
  KICK(300, CH_BOB, -14000), SET(300, CH_ROLL, 6), KICK(520, CH_BOB, -14000), SET(520, CH_ROLL, -6),
  KICK(740, CH_BOB, -14000), SET(740, CH_ROLL, 6), KICK(960, CH_BOB, -13000), SET(960, CH_ROLL, -6),
  KICK(1180, CH_BOB, -12000), SET(1180, CH_ROLL, 5), KICK(1400, CH_BOB, -11000), SET(1400, CH_ROLL, -4),
  KICK(1620, CH_BOB, -9000), SET(1620, CH_ROLL, 0), EMIT(1900, P_TEAR, 2, S_EYES),
  EXPR(2500, E_HAPPY), EXPR(3200, E_NEUTRAL), FIN(3600),
};
static const Key A_WINK[] = {
  EXPR(0, E_SMIRK), WINK(200, 3, 380), EMIT(420, P_SPARKLE, 1, S_TOP),
  EXPR(1600, E_NEUTRAL), FIN(2000),
};
static const Key A_CHEEKY[] = {
  EXPR(0, E_CHEEKY), K(0, OP_SET, CH_ROLL | 0x80, 12, 0), KICK(350, CH_BOB, -8000),
  EXPR(2000, E_NEUTRAL), SET(2000, CH_ROLL, 0), FIN(2600),
};
static const Key A_SURPRISED[] = {
  STYLE(0, ST_SNAPPY), EXPR(0, E_SURPRISED), KICK(0, CH_EYE, 300), KICK(0, CH_BOB, -12000),
  SET(0, CH_PITCH, -8), BLINK(450), STYLE(600, ST_NORMAL),
  EXPR(1500, E_HAPPY), SET(1500, CH_PITCH, 0), EXPR(2300, E_NEUTRAL), FIN(2800),
};
static const Key A_YAWN[] = {
  EXPR(0, E_SLEEPY), STYLE(500, ST_SLOW), EXPR(500, E_YAWN), SET(500, CH_PITCH, -12),
  STYLE(2200, ST_NORMAL), EXPR(2200, E_SLEEPY), SET(2200, CH_PITCH, 0), WINK(2600, 0, 300),
  EXPR(3200, E_NEUTRAL), BLINK(3300), FIN(4000),
};
static const Key A_LOVE[] = {
  EYES(0, EYES_HEART), EXPR(0, E_LOVE), KICK(0, CH_EYE, 250), EMIT(300, P_HEART, 2, S_AROUND),
  EMIT(1100, P_HEART, 2, S_AROUND), EMIT(1900, P_HEART, 2, S_AROUND), EYES(2900, EYES_NORMAL),
  EXPR(2900, E_HAPPY), EXPR(3400, E_NEUTRAL), FIN(3800),
};
static const Key A_DIZZY[] = {
  EYES(0, EYES_SPIN), EXPR(0, E_DIZZY), EMIT(0, P_STAR, 4, S_ORBIT), SET(0, CH_ROLL, 14),
  SET(400, CH_ROLL, -14), SET(800, CH_ROLL, 12), SET(1200, CH_ROLL, -12), SET(1600, CH_ROLL, 10),
  SET(2000, CH_ROLL, -8), SET(2400, CH_ROLL, 0), EYES(2600, EYES_NORMAL), EXPR(2600, E_NEUTRAL),
  SHAKE(2600, 12, 300), BLINK(2750), FIN(3400),
};
static const Key A_COOL[] = {
  EXPR(0, E_CURIOUS), GAZE(0, 0, -70), PROP(100, SLOT_EYES, PROP_SUNGLASSES),
  EXPR(650, E_COOL), GAZE(650, 0, 0), KICK(650, CH_BOB, -6000), EMIT(950, P_SPARKLE, 1, S_GLASSES),
  EMIT(2000, P_SPARKLE, 1, S_GLASSES), PROP(3300, SLOT_EYES, PROP_NONE), EXPR(3600, E_NEUTRAL),
  FIN(4100),
};
static const Key A_GRUMPY[] = {
  EXPR(0, E_ANGRY), SHAKE(600, 15, 900), EMIT(600, P_STEAM, 2, S_SIDES), EMIT(1100, P_STEAM, 2, S_SIDES),
  EMIT(1600, P_STEAM, 2, S_SIDES), EXPR(2400, E_SIGH), EMIT(2500, P_STEAM, 1, S_MOUTH),
  EXPR(3300, E_NEUTRAL), FIN(4200),
};
static const Key A_SNEEZE[] = {
  EXPR(0, E_SNEEZE_PRE), SET(500, CH_PITCH, -14), SET(500, CH_EYE, 108), SET(1000, CH_PITCH, -18),
  STYLE(1300, ST_SNAPPY), EXPR(1300, E_SNEEZE), KICK(1300, CH_BOB, 12000), SHAKE(1300, 25, 260),
  EMIT(1320, P_SPIT, 9, S_MOUTH), SET(1700, CH_PITCH, 0), STYLE(2000, ST_NORMAL), EXPR(2000, E_SNIFF),
  EXPR(2600, E_HAPPY), EXPR(3300, E_NEUTRAL), FIN(3800),
};
static const Key A_KISS[] = {
  EXPR(0, E_KISS), K(0, OP_SET, CH_ROLL | 0x80, -8, 0), WINK(450, 2, 500), EMIT(600, P_HEART, 1, S_MOUTH),
  EMIT(1100, P_HEART, 1, S_MOUTH), EXPR(2100, E_NEUTRAL), SET(2100, CH_ROLL, 0), FIN(2800),
};
static const Key A_EYEROLL[] = {
  EXPR(0, E_MEH), GAZE(200, -80, 20), GAZE(500, -60, -90), GAZE(800, 20, -100), GAZE(1100, 80, -60),
  GAZE(1400, 40, 30), SET(1500, CH_PITCH, -6), GAZE(1700, 0, 0), SET(1700, CH_PITCH, 0),
  EXPR(1700, E_SIGH), EXPR(2400, E_NEUTRAL), FIN(3000),
};
static const Key A_CRY[] = {
  EXPR(0, E_SAD), EXPR(600, E_CRY), SHAKE(600, 6, 1700), EMIT(600, P_TEAR, 2, S_EYES),
  EMIT(950, P_TEAR, 2, S_EYES), EMIT(1300, P_TEAR, 2, S_EYES), EMIT(1650, P_TEAR, 2, S_EYES),
  EMIT(2000, P_TEAR, 2, S_EYES), EXPR(2600, E_SNIFF), EXPR(3300, E_HAPPY), EXPR(4000, E_NEUTRAL),
  FIN(4400),
};
static const Key A_WHISTLE[] = {
  EXPR(0, E_WHISTLE), GAZE(0, 30, -70), OSC(200, CH_ROLL, 8, 1000), EMIT(300, P_NOTE, 1, S_MOUTH),
  EMIT(1000, P_NOTE, 1, S_MOUTH), EMIT(1700, P_NOTE, 1, S_MOUTH), EMIT(2400, P_NOTE, 1, S_MOUTH),
  OSC(3300, CH_ROLL, 0, 0), EXPR(3400, E_NEUTRAL), GAZE(3400, 0, 0), FIN(4200),
};
static const Key A_SILLY[] = {
  EXPR(0, E_SILLY), OSC(150, CH_ROLL, 10, 800), OSC(1800, CH_ROLL, 0, 0), EXPR(1900, E_NEUTRAL),
  BLINK(2150), FIN(2900),
};
static const Key A_HICCUP[] = {
  STYLE(0, ST_SNAPPY), EXPR(300, E_HIC), KICK(300, CH_BOB, -17000), EXPR(520, E_NEUTRAL),
  EXPR(1300, E_HIC), KICK(1300, CH_BOB, -17000), EXPR(1520, E_NEUTRAL), EXPR(2300, E_HIC),
  KICK(2300, CH_BOB, -17000), STYLE(2500, ST_NORMAL), EXPR(2500, E_EMBARRASSED), EXPR(3400, E_NEUTRAL),
  FIN(3800),
};
static const Key A_SUSPICIOUS[] = {
  STYLE(0, ST_SLOW), EXPR(0, E_SUSPICIOUS), GAZE(1300, 85, 5), STYLE(2600, ST_NORMAL),
  EXPR(2600, E_NEUTRAL), BLINK(2950), FIN(3500),
};
static const Key A_PARTY[] = {
  PROP(0, SLOT_HEAD, PROP_PARTYHAT), EXPR(0, E_SURPRISED), EXPR(400, E_LAUGH),
  EMIT(400, P_CONFETTI, 16, S_SKY), KICK(400, CH_BOB, -15000), KICK(700, CH_BOB, -15000),
  KICK(1000, CH_BOB, -14000), EMIT(900, P_CONFETTI, 12, S_SKY), EMIT(1600, P_CONFETTI, 10, S_SKY),
  EXPR(2400, E_HAPPY), PROP(3800, SLOT_HEAD, PROP_NONE), EXPR(4200, E_NEUTRAL), FIN(4600),
};
static const Key A_RAIN[] = {
  PROP(0, SLOT_SKY, PROP_CLOUD), GAZE(0, 0, -90), SET(0, CH_PITCH, -6), EXPR(0, E_WORRIED),
  WEATHER(600, P_RAIN, 220), BLINK(900), EXPR(1000, E_SAD), GAZE(1000, 0, -60),
  PROP(1900, SLOT_SKY, PROP_UMBRELLA), EXPR(2500, E_SMUG), GAZE(2500, 30, -50),
  KICK(2600, CH_BOB, -8000), WEATHER(4800, P_RAIN, 0), PROP(5200, SLOT_SKY, PROP_NONE),
  EXPR(5200, E_NEUTRAL), GAZE(5200, 0, 0), SET(5200, CH_PITCH, 0), FIN(6000),
};
static const Key A_SNOW[] = {
  WEATHER(0, P_SNOW, 80), GAZE(0, 20, -90), EXPR(0, E_WONDER), PROP(900, SLOT_HEAD, PROP_BEANIE),
  EXPR(1500, E_HAPPY), SET(1500, CH_CHEEK, 80), EXPR(2200, E_TONGUE_UP), EXPR(3400, E_HAPPY),
  SHAKE(3400, 9, 500), PROP(4600, SLOT_HEAD, PROP_NONE), WEATHER(4600, P_SNOW, 0),
  EXPR(5000, E_NEUTRAL), FIN(5600),
};
static const Key A_COLD[] = {
  PROP(0, SLOT_HEAD, PROP_BEANIE), EXPR(0, E_COLD), SHAKE(300, 12, 1600), OSC(300, CH_M_OPEN, 10, 160),
  EMIT(1000, P_STEAM, 1, S_MOUTH), OSC(1900, CH_M_OPEN, 0, 0), EMIT(2100, P_STEAM, 1, S_MOUTH),
  PROP(3300, SLOT_HEAD, PROP_NONE), EXPR(3300, E_NEUTRAL), FIN(4300),
};
static const Key A_HOT[] = {
  EXPR(0, E_HOT), OSC(0, CH_M_OPEN, 12, 300), OSC(1600, CH_M_OPEN, 0, 0), EXPR(1600, E_CURIOUS),
  PROP(1600, SLOT_EYES, PROP_SUNGLASSES), EXPR(2200, E_COOL), EMIT(2300, P_SPARKLE, 1, S_GLASSES),
  PROP(3600, SLOT_EYES, PROP_NONE), EXPR(4000, E_NEUTRAL), FIN(4600),
};
static const Key A_SUNNY[] = {
  EMIT(0, P_SPARKLE, 2, S_TOP), GAZE(0, -40, -80), EXPR(0, E_SQUINT), PROP(500, SLOT_EYES, PROP_SUNGLASSES),
  EXPR(1000, E_COOL), GAZE(1000, 0, 0), KICK(1000, CH_BOB, -6000), EMIT(1300, P_SPARKLE, 1, S_GLASSES),
  EMIT(2300, P_SPARKLE, 1, S_GLASSES), PROP(3500, SLOT_EYES, PROP_NONE), EXPR(3800, E_NEUTRAL),
  FIN(4300),
};
static const Key A_THUNDER[] = {
  PROP(0, SLOT_SKY, PROP_CLOUD), GAZE(0, 0, -80), EXPR(0, E_WORRIED), WEATHER(300, P_RAIN, 140),
  FLASH(1000), EMIT(1000, P_BOLT, 1, S_CLOUD), STYLE(1000, ST_SNAPPY), EXPR(1000, E_SCARED),
  KICK(1000, CH_EYE, 300), SHAKE(1000, 20, 500), FLASH(1200), STYLE(1400, ST_NORMAL),
  OSC(1600, CH_M_OPEN, 9, 150), SHAKE(1600, 8, 1200), OSC(2800, CH_M_OPEN, 0, 0),
  EXPR(2800, E_WORRIED), PROP(3600, SLOT_SKY, PROP_NONE), WEATHER(3600, P_RAIN, 0),
  EXPR(3600, E_NEUTRAL), GAZE(3600, 0, 0), FIN(4600),
};
static const Key A_FOG[] = {
  WEATHER(0, P_FOG, 15), EXPR(0, E_SQUINT), GAZEM(600, -70, 0), HEADM(600, -15, 0), GAZEM(1700, 70, 0),
  HEADM(1700, 15, 0), GAZE(2800, 0, 0), HEAD(2800, 0, 0), EXPR(2800, E_SHRUG), WEATHER(3600, P_FOG, 0),
  EXPR(3600, E_NEUTRAL), FIN(4400),
};
static const Key A_WIND[] = {
  EMIT(0, P_LEAF, 1, S_LEFT), GAZE(0, -90, 0), GAZE(350, -30, 10), GAZE(700, 40, -10), HEAD(700, 12, 0),
  GAZE(1100, 90, 0), HEAD(1100, 20, 0), EMIT(1300, P_LEAF, 2, S_LEFT), EXPR(1300, E_WINCE),
  SHAKE(1400, 6, 800), HEAD(2600, 0, 0), GAZE(2600, 0, 0), EXPR(2600, E_NEUTRAL), FIN(3600),
};
static const Key A_CLOUDY[] = {
  PROP(0, SLOT_SKY, PROP_CLOUD), K(0, OP_PROPDX, SLOT_SKY, -110, 110), STYLE(0, ST_SLOW),
  GAZE(0, -60, -90), GAZE(1500, 0, -90), GAZE(3000, 60, -90), STYLE(3600, ST_NORMAL),
  PROP(3800, SLOT_SKY, PROP_NONE), EXPR(3800, E_MEH), GAZE(3800, 0, 0), EXPR(4400, E_NEUTRAL),
  FIN(5000),
};
static const Key A_NIGHT[] = {
  EMIT(0, P_SPARKLE, 3, S_TOP), GAZE(0, 30, -90), EXPR(0, E_WONDER), EMIT(1200, P_SPARKLE, 2, S_TOP),
  EXPR(2000, E_YAWN), GAZE(2000, 0, 0), EXPR(3200, E_SLEEPY), FIN(4000),
};
static const Key A_HALLOWEEN[] = {
  PROP(0, SLOT_HEAD, PROP_WITCHHAT), EXPR(0, E_EVIL), OSC(500, CH_BOB, 400, 180),
  OSC(2200, CH_BOB, 0, 0), WINK(2300, 3, 420), PROP(3600, SLOT_HEAD, PROP_NONE),
  EXPR(4000, E_NEUTRAL), FIN(4600),
};
static const Key A_XMAS[] = {
  PROP(0, SLOT_HEAD, PROP_SANTAHAT), WEATHER(0, P_SNOW, 60), EXPR(0, E_HAPPY), EXPR(500, E_LAUGH),
  OSC(500, CH_BOB, 350, 220), OSC(2000, CH_BOB, 0, 0), EXPR(2000, E_HAPPY),
  EMIT(2000, P_SPARKLE, 2, S_TOP), PROP(3700, SLOT_HEAD, PROP_NONE), WEATHER(3700, P_SNOW, 0),
  EXPR(4000, E_NEUTRAL), FIN(4600),
};
static const Key A_CHARGING[] = {
  FLASH(0), EMIT(0, P_BOLT, 1, S_TOP), STYLE(0, ST_SNAPPY), EXPR(0, E_SHOCKED), SHAKE(0, 20, 300),
  STYLE(400, ST_NORMAL), EXPR(700, E_BLISS), EMIT(900, P_SPARKLE, 2, S_TOP),
  EXPR(2600, E_NEUTRAL), FIN(3400),
};
static const Key A_LOW_BATTERY[] = {
  STYLE(0, ST_SLOW), EXPR(0, E_TIRED), SET(0, CH_PITCH, 8), EMIT(1200, P_SWEAT, 1, S_TEMPLE),
  WINK(2400, 0, 500), STYLE(3000, ST_NORMAL), FIN(3600),
};
static const Key A_BT_LOST[] = {
  EXPR(0, E_WORRIED), GAZE(0, -80, 0), HEAD(0, -20, 0), GAZE(1000, 80, 0), HEAD(1000, 20, 0),
  GAZE(2000, 0, 0), HEAD(2000, 0, 0), EXPR(2000, E_SAD), FIN(3000),
};
static const Key A_SLEEP[] = {
  EXPR(0, E_YAWN), PROP(1500, SLOT_HEAD, PROP_NIGHTCAP), EXPR(1500, E_SLEEPY), EXPR(2400, E_ASLEEP),
  EMIT(2800, P_ZZZ, 1, S_ZZZ), EMIT(3600, P_ZZZ, 1, S_ZZZ), FIN(4000),
};
static const Key A_WAKE[] = {
  EXPR(0, E_GROGGY), BLINK(100), BLINK(500), PROP(600, SLOT_HEAD, PROP_NONE), EXPR(900, E_YAWN),
  EXPR(2200, E_SLEEPY), FIN(3200),
};
static const Key A_POKED[] = {
  STYLE(0, ST_SNAPPY), EXPR(0, E_SURPRISED), KICK(0, CH_BOB, -16000), KICK(0, CH_EYE, 250),
  SHAKE(0, 16, 250), STYLE(400, ST_NORMAL), BLINK(400), EXPR(900, E_HAPPY), EXPR(1600, E_NEUTRAL),
  FIN(2000),
};

static const Key *const ACTS[ACT_COUNT] = {
  [ACT_GLANCE] = A_GLANCE, [ACT_LOOK_AROUND] = A_LOOK_AROUND, [ACT_LAUGH] = A_LAUGH,
  [ACT_WINK] = A_WINK, [ACT_CHEEKY] = A_CHEEKY, [ACT_SURPRISED] = A_SURPRISED, [ACT_YAWN] = A_YAWN,
  [ACT_LOVE] = A_LOVE, [ACT_DIZZY] = A_DIZZY, [ACT_COOL] = A_COOL, [ACT_GRUMPY] = A_GRUMPY,
  [ACT_SNEEZE] = A_SNEEZE, [ACT_KISS] = A_KISS, [ACT_EYEROLL] = A_EYEROLL, [ACT_CRY] = A_CRY,
  [ACT_WHISTLE] = A_WHISTLE, [ACT_SILLY] = A_SILLY, [ACT_HICCUP] = A_HICCUP,
  [ACT_SUSPICIOUS] = A_SUSPICIOUS, [ACT_PARTY] = A_PARTY, [ACT_RAIN] = A_RAIN, [ACT_SNOW] = A_SNOW,
  [ACT_COLD] = A_COLD, [ACT_HOT] = A_HOT, [ACT_SUNNY] = A_SUNNY, [ACT_THUNDER] = A_THUNDER,
  [ACT_FOG] = A_FOG, [ACT_WIND] = A_WIND, [ACT_CLOUDY] = A_CLOUDY, [ACT_NIGHT] = A_NIGHT,
  [ACT_HALLOWEEN] = A_HALLOWEEN, [ACT_XMAS] = A_XMAS, [ACT_CHARGING] = A_CHARGING,
  [ACT_LOW_BATTERY] = A_LOW_BATTERY, [ACT_BT_LOST] = A_BT_LOST, [ACT_SLEEP] = A_SLEEP,
  [ACT_WAKE] = A_WAKE, [ACT_POKED] = A_POKED,
};

// ----------------------------------------------------------------- state ----

static ActId s_act = ACT_NONE;
static const Key *s_key;      // next key to fire
static float s_act_t;         // s since the act started
static float s_mirror = 1;
static bool s_frozen;

static bool s_idle = true;
static float s_next_saccade = 1.5f, s_next_blink = 3.0f;

typedef struct {
  float t, dur, hold;
  uint8_t side;   // 0 both, 1 left, 2 right
  bool on;
} Blink;
static Blink s_blink;

static float s_shake_amp, s_shake_left, s_clock;

typedef struct {
  uint8_t ch;
  float amp, period, phase;
} Osc;
#define MAX_OSC 3
static Osc s_osc[MAX_OSC];

typedef struct {
  float v, target;            // show spring
  float dxv, dxt;             // horizontal drift spring
  uint8_t next;
} PropAnim;
static PropAnim s_prop[SLOT_COUNT];
static bool s_props_on = true;

static Mood s_mood = MOOD_NORMAL;
static Weather s_wx;
static bool s_wx_on;
static bool s_ambient;
static uint8_t s_emit_kind;   // weather particles spawned by the current act
static float s_emit_rate, s_emit_acc;

static ActId s_recent[3];

// --------------------------------------------------------------- springs ----

static void set_target(int ch, float v) {
  if (ch == X_LT) {
    s_sp[CH_LT_L].t = s_sp[CH_LT_R].t = v;
  } else if (ch == X_LB) {
    s_sp[CH_LB_L].t = s_sp[CH_LB_R].t = v;
  } else if (ch < CH_COUNT) {
    s_sp[ch].t = v;
  }
}

// Values in tables are x100; head angles are radians x100, px channels px x100.
static float table_value(int ch, int16_t v) { return v / 100.0f; }

static void apply_expr_list(const EV *e) {
  for (; e->ch != 255; e++) set_target(e->ch, table_value(e->ch, e->v));
}

static const EV *mood_expr(void) {
  switch (s_mood) {
    case MOOD_SLEEPY: return X_SLEEPY;
    case MOOD_ASLEEP: return X_ASLEEP;
    case MOOD_TIRED: return X_TIRED;
    case MOOD_WORRIED: return X_WORRIED;
    default: return X_NEUTRAL;
  }
}

// The resting face: neutral, the mood on top, then a hint of the weather.
static void apply_baseline_extras(void) {
  if (!s_wx_on || s_wx.kind == WX_UNKNOWN) return;
  if (s_wx.temp_c <= 3) set_target(CH_CHEEK, 0.55f);
  if (s_wx.temp_c >= 27) {
    set_target(CH_SWEAT, 0.7f);
    set_target(X_LT, 0.22f);
  }
  if (s_wx.kind == WX_RAIN || s_wx.kind == WX_DRIZZLE || s_wx.kind == WX_THUNDER) {
    set_target(CH_BROW_TILT, -0.25f);
    set_target(CH_M_CURVE, 0.25f);
  }
  if (s_wx.kind == WX_FOG) set_target(X_LT, 0.25f);
}

static void apply_expr(int e) {
  for (int c = 0; c < CH_COUNT; c++) s_sp[c].t = s_neutral[c];
  if (e == E_NEUTRAL) {
    apply_expr_list(mood_expr());
    apply_baseline_extras();
  } else {
    apply_expr_list(EXPRS[e]);
  }
}

static bool spring_settled(int c) {
  float scale = PX_CHANNEL(c) ? 20.0f : 1.0f;
  float d = s_sp[c].x - s_sp[c].t;
  return fabs_(d) < 0.004f * scale && fabs_(s_sp[c].v) < 0.03f * scale;
}

static void step_springs(float dt) {
  for (int c = 0; c < CH_COUNT; c++) {
    Spring *s = &s_sp[c];
    if (spring_settled(c)) {
      s->x = s->t;
      s->v = 0;
      continue;
    }
    float k = s_k[c] * s_style, d = s_d[c] * (s_style < 1 ? 0.75f : s_style > 1 ? 1.35f : 1);
    s->v += (k * (s->t - s->x) - d * s->v) * dt;
    s->x += s->v * dt;
  }
}

// ------------------------------------------------------------- particles ----

static const uint8_t CONFETTI_COLS[] = { RGB(3, 0, 0), RGB(0, 2, 3), RGB(0, 3, 1), RGB(3, 0, 3),
                                         RGB(1, 1, 3), RGB(3, 2, 0), RGB(0, 3, 3) };

static Particle *spawn(uint8_t kind) {
  Particle *best = NULL;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle *p = &s_face.particles[i];
    if (p->kind == P_NONE) {
      best = p;
      break;
    }
    // Recycle the oldest weather particle rather than drop an act's.
    if (p->ambient && (!best || p->age / p->life > best->age / best->life)) best = p;
  }
  if (!best) return NULL;
  memset(best, 0, sizeof(*best));
  best->kind = kind;
  best->size = 1;
  best->life = 2;
  return best;
}

static float eye_x(int side) { return CX + side * 36 + s_sp[CH_YAW].x * 140; }
static float eye_y(void) { return CY - 28 + s_sp[CH_PITCH].x * 140 + s_sp[CH_BOB].x; }
static float mouth_x(void) { return CX + s_sp[CH_M_X].x + s_sp[CH_YAW].x * 145; }
static float mouth_y(void) { return CY + 30 + s_sp[CH_PITCH].x * 140 + s_sp[CH_BOB].x; }

static void emit(uint8_t kind, int n, int src) {
  for (int i = 0; i < n; i++) {
    Particle *p = spawn(kind);
    if (!p) return;
    switch (src) {
      case S_MOUTH:
        p->x = mouth_x() + frand(-4, 4);
        p->y = mouth_y();
        break;
      case S_EYES: {
        int side = (i & 1) ? 1 : -1;
        p->x = eye_x(side) + side * 16;
        p->y = eye_y() + 20;
        break;
      }
      case S_SIDES: {
        // Out of the top of the head, either side.
        int side = (i & 1) ? 1 : -1;
        p->x = CX + side * frand(68, 80);
        p->y = CY - frand(58, 70);
        p->vx = side * frand(25, 40);
        break;
      }
      case S_ORBIT:
        p->angle = 6.2832f * i / (n > 0 ? n : 1);
        p->spin = 3.2f;
        break;
      case S_SKY:
        p->x = frand(30, 230);
        p->y = frand(-30, -4);
        break;
      case S_GLASSES:
        p->x = eye_x(1) + 8;
        p->y = eye_y() - 10;
        break;
      case S_ZZZ:
        p->x = CX + 44 + s_sp[CH_YAW].x * 140;
        p->y = CY - 64;
        break;
      case S_LEFT:
        p->x = -12;
        p->y = frand(70, 190);
        break;
      case S_CLOUD:
        p->x = CX + s_face.props[SLOT_SKY].dx + frand(-20, 20);
        p->y = 50;
        break;
      case S_TOP:
        p->x = frand(60, 200);
        p->y = frand(20, 70);
        break;
      case S_TEMPLE:
        p->x = CX + 82;
        p->y = CY - 64;
        break;
      case S_AROUND:
        p->x = CX + frand(-80, 80);
        p->y = CY + frand(-20, 40);
        break;
    }
    switch (kind) {
      case P_HEART:
        p->vx = frand(-15, 15);
        p->vy = frand(-60, -40);
        p->life = 2.0f;
        p->size = frand(0.9f, 1.4f);
        p->color = (rand() & 3) ? RGB(3, 0, 0) : RGB(3, 0, 1);
        p->spin = frand(0, 6);
        break;
      case P_NOTE:
        p->vx = frand(25, 45);
        p->vy = frand(-40, -25);
        p->life = 2.2f;
        p->spin = frand(0, 6);
        break;
      case P_STEAM:
        if (src == S_MOUTH) p->vx = frand(-6, 6);
        p->vy = -22;
        p->life = 1.1f;
        break;
      case P_SPIT:
        p->vx = frand(-110, 110);
        p->vy = frand(-40, 70);
        p->life = 0.55f;
        p->size = frand(0.7f, 1.2f);
        break;
      case P_TEAR:
        p->vx = frand(-4, 4) + ((i & 1) ? 12 : -12);
        p->vy = 10;
        p->life = 1.3f;
        break;
      case P_SWEAT:
        p->vy = 6;
        p->life = 1.8f;
        p->size = 1.2f;
        break;
      case P_ZZZ:
        p->vx = 16;
        p->vy = -22;
        p->life = 2.6f;
        p->angle = -0.15f;
        break;
      case P_STAR:
        p->life = 2.6f;
        p->size = 0.75f;
        break;
      case P_SPARKLE:
        p->life = src == S_TOP ? frand(1.2f, 2.0f) : frand(0.6f, 0.9f);
        p->size = src == S_GLASSES ? 1.3f : frand(0.6f, 1.0f);
        break;
      case P_CONFETTI:
        p->vx = frand(-25, 25);
        p->vy = frand(40, 90);
        p->life = 4.0f;
        p->color = CONFETTI_COLS[rand() % sizeof(CONFETTI_COLS)];
        p->spin = frand(6, 14);
        p->angle = frand(0, 6.28f);
        break;
      case P_LEAF:
        p->vx = frand(130, 170);
        p->vy = frand(-15, 15);
        p->life = 2.4f;
        p->spin = frand(4, 8);
        break;
      case P_BOLT:
        p->life = 0.4f;
        p->x += 30;
        p->y += 30;
        break;
    }
  }
}

static void emit_weather(uint8_t kind, bool ambient) {
  Particle *p = spawn(kind);
  if (!p) return;
  p->ambient = ambient;
  const PropState *sky = &s_face.props[SLOT_SKY];
  bool cloud = sky->id == PROP_CLOUD && sky->show > 0.7f;
  switch (kind) {
    case P_RAIN:
      p->x = cloud ? CX + sky->dx + frand(-45, 45) : frand(-30, 270);
      p->y = cloud ? 40 : frand(-20, -2);
      p->vx = 30 + s_wx.wind_kmh * 0.6f;
      p->vy = frand(280, 340);
      p->life = 1.2f;
      p->size = frand(0.8f, 1.2f);
      break;
    case P_SNOW:
      p->x = frand(-10, 270);
      p->y = -6;
      p->vy = frand(28, 46);
      p->life = 8;
      p->size = frand(0.7f, 1.4f);
      p->spin = frand(0, 6);
      break;
    case P_FOG:
      p->x = frand(-40, 200);
      p->y = frand(60, 200);
      p->vx = frand(10, 22);
      p->life = 5;
      p->size = frand(0.8f, 1.2f);
      break;
    case P_LEAF:
      p->x = -12;
      p->y = frand(60, 200);
      p->vx = frand(120, 170);
      p->vy = frand(-15, 15);
      p->life = 2.4f;
      p->spin = frand(4, 8);
      break;
    case P_ZZZ:
      p->x = CX + 44 + s_sp[CH_YAW].x * 140;
      p->y = CY - 64;
      p->vx = 16;
      p->vy = -22;
      p->life = 2.6f;
      p->angle = -0.15f;
      break;
  }
}

static void step_particles(float dt) {
  const PropState *sky = &s_face.props[SLOT_SKY];
  bool umbrella = sky->id == PROP_UMBRELLA && sky->show > 0.85f;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle *p = &s_face.particles[i];
    if (p->kind == P_NONE) continue;
    p->age += dt;
    if (p->age >= p->life) {
      p->kind = P_NONE;
      continue;
    }
    switch (p->kind) {
      case P_TEAR:
        p->vy += 260 * dt;
        break;
      case P_SPIT:
      case P_SPLASH:
        p->vy += 450 * dt;
        break;
      case P_SNOW:
        p->vx = fsin(p->age * 2.0f + p->spin) * 14 + s_wx.wind_kmh * 0.3f;
        break;
      case P_HEART:
      case P_NOTE:
        p->vx += fsin(p->age * 5 + p->spin) * 60 * dt;
        p->angle = fsin(p->age * 4 + p->spin) * 0.3f;
        break;
      case P_CONFETTI:
        p->vy += (70 - p->vy) * 2 * dt;
        p->vx = fsin(p->age * 5 + p->spin) * 35;
        p->angle += p->spin * dt;
        break;
      case P_LEAF:
        p->angle += p->spin * dt;
        p->vy = fsin(p->age * 4 + p->spin) * 40;
        break;
      case P_STEAM:
        p->vy *= 1 - 1.5f * dt;
        break;
      case P_STAR: {
        float a = p->angle + p->age * p->spin;
        p->x = CX + fcos(a) * 72 + s_sp[CH_YAW].x * 140;
        p->y = CY - 100 + fsin(a) * 16 + s_sp[CH_BOB].x;
        break;
      }
    }
    if (p->kind != P_STAR) {
      p->x += p->vx * dt;
      p->y += p->vy * dt;
    }
    if (p->y > SCREEN_H + 20 || p->x > SCREEN_W + 60 || p->x < -60) {
      p->kind = P_NONE;
      continue;
    }
    if (umbrella && p->kind == P_RAIN && p->y > 34 && p->y < 60 && p->x > CX - 100 && p->x < CX + 100) {
      // Hits the umbrella: a couple of splashes bounce off the canopy.
      float x = p->x, y = 36 + fabs_(p->x - CX) * 0.12f;
      p->kind = P_NONE;
      for (int k = 0; k < 2; k++) {
        Particle *sp = spawn(P_SPLASH);
        if (!sp) break;
        sp->ambient = true;
        sp->x = x;
        sp->y = y;
        sp->vx = frand(-60, 60);
        sp->vy = frand(-90, -40);
        sp->life = 0.35f;
      }
    }
  }
}

// ------------------------------------------------------------------ keys ----

static void fire(const Key *k) {
  float m = (k->a & MIRROR) ? s_mirror : 1;
  switch (k->op) {
    case OP_EXPR:
      apply_expr(k->a);
      break;
    case OP_SET: {
      int ch = k->a & 0x7F;
      float v = k->b / 100.0f;
      if (k->a & 0x80) v *= s_mirror;
      set_target(ch, v);
      break;
    }
    case OP_SNAP:
      if (k->a < CH_COUNT) {
        s_sp[k->a].x = s_sp[k->a].t = k->b / 100.0f;
        s_sp[k->a].v = 0;
      }
      break;
    case OP_GAZE:
      set_target(CH_GAZE_X, k->b / 100.0f * m);
      set_target(CH_GAZE_Y, k->c / 100.0f);
      break;
    case OP_HEAD:
      set_target(CH_YAW, k->b / 100.0f * m);
      set_target(CH_PITCH, k->c / 100.0f);
      break;
    case OP_BLINK: {
      int side = k->a;
      if (side == 3) side = s_mirror > 0 ? 1 : 2;
      s_blink = (Blink){ 0, k->b ? 0.24f : 0.16f, k->b / 1000.0f, (uint8_t)side, true };
      break;
    }
    case OP_EYES:
      s_face.eye_mode = k->a;
      break;
    case OP_PROP:
      if (s_props_on || k->b == PROP_NONE) {
        PropAnim *pa = &s_prop[k->a];
        pa->next = (uint8_t)k->b;
        if (s_face.props[k->a].id == PROP_NONE) {
          s_face.props[k->a].id = pa->next;
          s_face.props[k->a].show = 0;
          s_face.props[k->a].dx = 0;
          pa->dxt = 0;
        }
        pa->target = (pa->next != PROP_NONE && s_face.props[k->a].id == pa->next) ? 1 : 0;
      }
      break;
    case OP_PROPDX:
      s_face.props[k->a].dx = k->b;
      s_prop[k->a].dxv = 0;
      s_prop[k->a].dxt = k->c;
      break;
    case OP_EMIT:
      emit(k->a, k->b, k->c);
      break;
    case OP_SHAKE:
      s_shake_amp = k->b / 10.0f;
      s_shake_left = k->c / 1000.0f;
      break;
    case OP_KICK:
      if (k->a < CH_COUNT) s_sp[k->a].v += k->b / 100.0f;
      break;
    case OP_FLASH:
      s_face.flash = 1;
      break;
    case OP_STYLE:
      s_style = k->a == ST_SLOW ? 0.35f : k->a == ST_SNAPPY ? 2.0f : 1.0f;
      break;
    case OP_WEATHER:
      s_emit_kind = k->a;
      s_emit_rate = k->b / 10.0f;
      break;
    case OP_OSC: {
      Osc *slot = NULL;
      for (int i = 0; i < MAX_OSC; i++) {
        if (s_osc[i].amp != 0 && s_osc[i].ch == k->a) slot = &s_osc[i];
      }
      if (!slot) {
        for (int i = 0; i < MAX_OSC; i++) {
          if (s_osc[i].amp == 0) {
            slot = &s_osc[i];
            break;
          }
        }
      }
      if (!slot) break;
      float amp = k->b / 100.0f;
      if (k->a == CH_ROLL || k->a == CH_YAW || k->a == CH_PITCH) amp /= 1.0f;
      *slot = (Osc){ k->a, amp, k->c / 1000.0f, 0 };
      break;
    }
  }
}

static void end_act(void) {
  s_act = ACT_NONE;
  s_key = NULL;
  s_style = 1;
  s_emit_kind = P_NONE;
  s_emit_rate = 0;
  s_face.eye_mode = EYES_NORMAL;
  for (int i = 0; i < MAX_OSC; i++) s_osc[i].amp = 0;
  apply_expr(E_NEUTRAL);
  set_target(CH_GAZE_X, 0);
  set_target(CH_GAZE_Y, 0);
  s_next_saccade = frand(0.8f, 2.0f);
}

void anim_play(ActId act) {
  if (act <= ACT_NONE || act >= ACT_COUNT || !ACTS[act]) return;
  if (s_act != ACT_NONE) end_act();
  s_act = act;
  s_key = ACTS[act];
  s_act_t = 0;
  s_mirror = (rand() & 1) ? 1 : -1;
  s_recent[2] = s_recent[1];
  s_recent[1] = s_recent[0];
  s_recent[0] = act;
}

bool anim_playing(void) { return s_act != ACT_NONE; }
ActId anim_current(void) { return s_act; }

static void step_act(float dt) {
  if (s_act == ACT_NONE) return;
  s_act_t += dt;
  int ms = (int)(s_act_t * 1000);
  while (s_key && s_key->t <= ms) {
    if (s_key->op == OP_END) {
      end_act();
      return;
    }
    fire(s_key);
    s_key++;
  }
}

// -------------------------------------------------------------- idle life ----

static void step_idle(float dt) {
  if (!s_idle || s_act != ACT_NONE || s_mood == MOOD_ASLEEP) return;
  s_next_saccade -= dt;
  if (s_next_saccade <= 0) {
    int r = rand() % 100;
    float gx, gy;
    if (r < 12) {
      gx = 0, gy = 0.85f;   // a glance down at the time
    } else if (r < 32) {
      gx = frand(-0.9f, 0.9f), gy = frand(-0.7f, 0.5f);
    } else {
      gx = frand(-0.45f, 0.45f), gy = frand(-0.3f, 0.3f);
    }
    set_target(CH_GAZE_X, gx);
    set_target(CH_GAZE_Y, gy);
    // The head follows the eyes a little, a beat later via its softer spring.
    set_target(CH_YAW, gx * 0.07f);
    set_target(CH_PITCH, gy * 0.04f);
    if (rand() % 100 < 12) s_sp[CH_BROW_Y].v += 28;   // an eyebrow flick
    s_next_saccade = frand(1.0f, 3.2f);
  }
  s_next_blink -= dt;
  if (s_next_blink <= 0 && !s_blink.on) {
    anim_blink();
    // Sometimes a double blink.
    s_next_blink = (rand() % 100 < 15) ? 0.32f : frand(2.4f, 6.0f);
  }
}

void anim_set_idle(bool on) {
  if (s_idle == on) return;
  s_idle = on;
  if (!on && s_act == ACT_NONE) {
    set_target(CH_GAZE_X, 0);
    set_target(CH_GAZE_Y, 0);
    set_target(CH_YAW, 0);
    set_target(CH_PITCH, 0);
  }
}

void anim_blink(void) { s_blink = (Blink){ 0, 0.16f, 0, 0, true }; }

// ------------------------------------------------------------------ frame ----

static void compose(void) {
  float o[CH_COUNT];
  for (int c = 0; c < CH_COUNT; c++) o[c] = s_sp[c].x;
  for (int i = 0; i < MAX_OSC; i++) {
    if (s_osc[i].amp != 0 && s_osc[i].period > 0) {
      o[s_osc[i].ch] += s_osc[i].amp * fsin(s_osc[i].phase);
    }
  }
  FaceState *f = &s_face;
  f->yaw = o[CH_YAW];
  f->pitch = o[CH_PITCH];
  f->roll = o[CH_ROLL];
  f->bob = o[CH_BOB];
  f->scale = o[CH_SCALE];
  f->gaze_x = o[CH_GAZE_X];
  f->gaze_y = o[CH_GAZE_Y];
  f->cross = o[CH_CROSS];
  f->pupil = o[CH_PUPIL];
  f->eye_scale = o[CH_EYE];
  f->eye_l = o[CH_EYE_L];
  f->eye_r = o[CH_EYE_R];
  f->lid_top_l = o[CH_LT_L];
  f->lid_top_r = o[CH_LT_R];
  f->lid_bot_l = o[CH_LB_L];
  f->lid_bot_r = o[CH_LB_R];
  f->lid_tilt = o[CH_LID_TILT];
  f->happy = o[CH_HAPPY];
  f->brow_y = o[CH_BROW_Y];
  f->brow_tilt = o[CH_BROW_TILT];
  f->brow_l = o[CH_BROW_L];
  f->brow_r = o[CH_BROW_R];
  f->brow_arch = o[CH_BROW_ARCH];
  f->mouth_open = o[CH_M_OPEN];
  f->mouth_w = o[CH_M_W];
  f->mouth_curve = o[CH_M_CURVE];
  f->mouth_round = o[CH_M_ROUND];
  f->mouth_skew = o[CH_M_SKEW];
  f->mouth_x = o[CH_M_X];
  f->teeth = o[CH_TEETH];
  f->tongue = o[CH_TONGUE];
  f->cheek = o[CH_CHEEK];
  f->flush = o[CH_FLUSH];
  f->sweat = o[CH_SWEAT];

  if (s_blink.on) {
    // Down fast, hold (for a wink), up a little slower.
    float t = s_blink.t, d = s_blink.dur, h = s_blink.hold;
    float b;
    if (t < d * 0.4f) {
      b = t / (d * 0.4f);
    } else if (t < d * 0.4f + h) {
      b = 1;
    } else {
      b = 1 - (t - d * 0.4f - h) / (d * 0.6f);
    }
    b = fclamp(b, 0, 1);
    // The bottom lid comes up to meet the top one, so the eye shuts a little
    // below centre rather than at the bottom edge.
    float bb = 0.6f * b;
    if (s_blink.side != 2) {
      f->lid_top_l = f->lid_top_l > b ? f->lid_top_l : b;
      f->lid_bot_l = f->lid_bot_l > bb ? f->lid_bot_l : bb;
    }
    if (s_blink.side != 1) {
      f->lid_top_r = f->lid_top_r > b ? f->lid_top_r : b;
      f->lid_bot_r = f->lid_bot_r > bb ? f->lid_bot_r : bb;
    }
  }
  if (s_shake_left > 0) {
    f->shake_x = s_shake_amp * fsin(s_clock * 71);
    f->shake_y = s_shake_amp * 0.5f * fcos(s_clock * 53);
  } else {
    f->shake_x = f->shake_y = 0;
  }
}

static void step_props(float dt) {
  for (int s = 0; s < SLOT_COUNT; s++) {
    PropState *ps = &s_face.props[s];
    PropAnim *pa = &s_prop[s];
    if (ps->id == PROP_NONE && pa->next == PROP_NONE) continue;
    if (ps->id != pa->next) {
      // Swap: hide the old one, then bring in the new one.
      pa->target = 0;
      if (ps->show < 0.03f) {
        ps->id = pa->next;
        ps->show = 0;
        pa->v = 0;
        pa->target = ps->id != PROP_NONE ? 1 : 0;
        if (ps->id == PROP_NONE) continue;
      }
    }
    // Bouncy on the way in, quick on the way out.
    float k = pa->target > 0 ? 130 : 200, d = pa->target > 0 ? 11 : 26;
    pa->v += (k * (pa->target - ps->show) - d * pa->v) * dt;
    ps->show += pa->v * dt;
    if (pa->target == 0 && ps->show < 0.01f && pa->next == PROP_NONE) {
      ps->id = PROP_NONE;
      ps->show = 0;
      pa->v = 0;
    }
    float kd = 3.0f, dd = 3.4f;
    pa->dxv += (kd * (pa->dxt - ps->dx) - dd * pa->dxv) * dt;
    ps->dx += pa->dxv * dt;
  }
}

static void step(float dt) {
  s_clock += dt;
  step_act(dt);
  step_idle(dt);
  if (s_blink.on) {
    s_blink.t += dt;
    if (s_blink.t >= s_blink.dur + s_blink.hold) s_blink.on = false;
  }
  if (s_shake_left > 0) s_shake_left -= dt;
  for (int i = 0; i < MAX_OSC; i++) {
    if (s_osc[i].amp != 0 && s_osc[i].period > 0) s_osc[i].phase += 6.2832f * dt / s_osc[i].period;
  }
  step_springs(dt);
  step_props(dt);
  if (s_face.flash > 0) s_face.flash -= dt * 6;
  if (s_face.flash < 0) s_face.flash = 0;
  s_face.eye_phase += dt * 7;

  // Weather particles: from the current act, or ambient while frames run.
  uint8_t kind = s_emit_kind;
  float rate = s_emit_rate;
  bool ambient = false;
  if (kind == P_NONE && s_ambient && s_wx_on) {
    ambient = true;
    switch (s_wx.kind) {
      case WX_DRIZZLE: kind = P_RAIN, rate = 6; break;
      case WX_RAIN: kind = P_RAIN, rate = 12; break;
      case WX_THUNDER: kind = P_RAIN, rate = 16; break;
      case WX_SNOW: kind = P_SNOW, rate = 4; break;
      case WX_FOG: kind = P_FOG, rate = 0.5f; break;
      default:
        if (s_wx.wind_kmh >= 35) kind = P_LEAF, rate = 0.3f;
        break;
    }
  }
  if (kind == P_NONE && s_ambient && s_mood == MOOD_ASLEEP) {
    kind = P_ZZZ;
    rate = 0.6f;
    ambient = true;
  }
  if (kind != P_NONE && rate > 0) {
    s_emit_acc += rate * dt;
    while (s_emit_acc >= 1) {
      emit_weather(kind, ambient);
      s_emit_acc -= 1;
    }
  }
  step_particles(dt);
  compose();
}

void anim_step(float dt) {
  if (s_frozen) return;
  // Two substeps keep the stiff lid springs stable at 30fps.
  int n = dt > 0.02f ? 2 : 1;
  for (int i = 0; i < n; i++) step(dt / n);
}

bool anim_busy(void) {
  if (s_act != ACT_NONE || s_blink.on || s_shake_left > 0 || s_face.flash > 0) return true;
  for (int i = 0; i < MAX_OSC; i++) {
    if (s_osc[i].amp != 0) return true;
  }
  for (int c = 0; c < CH_COUNT; c++) {
    if (!spring_settled(c)) return true;
  }
  for (int s = 0; s < SLOT_COUNT; s++) {
    if (s_face.props[s].id != s_prop[s].next) return true;
    if (s_face.props[s].id != PROP_NONE &&
        (fabs_(s_prop[s].v) > 0.01f || fabs_(s_face.props[s].show - s_prop[s].target) > 0.01f)) {
      return true;
    }
  }
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (s_face.particles[i].kind != P_NONE && !s_face.particles[i].ambient) return true;
  }
  return false;
}

const FaceState *anim_face(void) { return &s_face; }

void anim_set_mood(Mood mood) {
  if (mood == s_mood) return;
  s_mood = mood;
  if (s_act == ACT_NONE) apply_expr(E_NEUTRAL);
}

void anim_set_weather(const Weather *wx, bool on) {
  s_wx = *wx;
  s_wx_on = on;
  if (s_act == ACT_NONE) apply_expr(E_NEUTRAL);
}

void anim_set_ambient(bool on) { s_ambient = on; }

void anim_set_props(bool on) {
  s_props_on = on;
  if (!on) {
    for (int s = 0; s < SLOT_COUNT; s++) s_prop[s].next = PROP_NONE;
  }
}

ActId anim_weather_act(const Weather *wx) {
  if (wx->kind == WX_UNKNOWN) return ACT_NONE;
  if (wx->wind_kmh >= 35 && rand() % 100 < 50) return ACT_WIND;
  switch (wx->kind) {
    case WX_DRIZZLE:
    case WX_RAIN: return ACT_RAIN;
    case WX_SNOW: return ACT_SNOW;
    case WX_THUNDER: return ACT_THUNDER;
    case WX_FOG: return ACT_FOG;
    case WX_CLOUDY: return ACT_CLOUDY;
    default:
      if (wx->temp_c >= 26) return ACT_HOT;
      if (wx->temp_c <= 3) return ACT_COLD;
      return wx->is_day ? ACT_SUNNY : ACT_NIGHT;
  }
}

void anim_clear_props(void) {
  for (int s = 0; s < SLOT_COUNT; s++) {
    s_prop[s].next = PROP_NONE;
    s_prop[s].target = 0;
  }
}

void anim_settle(void) {
  if (s_frozen) return;
  for (int i = 0; i < 300 && anim_busy(); i++) step(0.033f);
  compose();
}

static bool recent(ActId a) { return s_recent[0] == a || s_recent[1] == a || s_recent[2] == a; }

ActId anim_pick(const Weather *wx, int month, int mday, bool weather_on, bool seasonal_on,
                bool props_on) {
  if (seasonal_on && rand() % 100 < 30) {
    if (month == 9 && mday >= 20 && !recent(ACT_HALLOWEEN)) return ACT_HALLOWEEN;
    if (month == 11 && mday <= 26 && !recent(ACT_XMAS)) return ACT_XMAS;
  }
  if (weather_on && wx->kind != WX_UNKNOWN && rand() % 100 < 40) {
    ActId a = anim_weather_act(wx);
    if (a != ACT_NONE && !recent(a)) return a;
  }
  // Everyday acts, glances twice as likely as the rest.
  for (int tries = 0; tries < 8; tries++) {
    int r = rand() % (ACT_SUSPICIOUS - ACT_FIRST_FUN + 3);
    ActId a = r >= ACT_SUSPICIOUS - ACT_FIRST_FUN + 1 ? ACT_GLANCE : (ActId)(ACT_FIRST_FUN + r);
    if (!props_on && a == ACT_COOL) continue;
    if (a != ACT_GLANCE && recent(a)) continue;
    return a;
  }
  return ACT_GLANCE;
}

void anim_init(void) {
  memset(&s_face, 0, sizeof(s_face));
  face_neutral(&s_face);
  float *ptrs[CH_COUNT] = {
    &s_face.yaw, &s_face.pitch, &s_face.roll, &s_face.bob, &s_face.scale,
    &s_face.gaze_x, &s_face.gaze_y, &s_face.cross, &s_face.pupil, &s_face.eye_scale,
    &s_face.eye_l, &s_face.eye_r, &s_face.lid_top_l, &s_face.lid_top_r, &s_face.lid_bot_l,
    &s_face.lid_bot_r, &s_face.lid_tilt, &s_face.happy, &s_face.brow_y, &s_face.brow_tilt,
    &s_face.brow_l, &s_face.brow_r, &s_face.brow_arch, &s_face.mouth_open, &s_face.mouth_w,
    &s_face.mouth_curve, &s_face.mouth_round, &s_face.mouth_skew, &s_face.mouth_x, &s_face.teeth,
    &s_face.tongue, &s_face.cheek, &s_face.flush, &s_face.sweat,
  };
  for (int c = 0; c < CH_COUNT; c++) {
    s_neutral[c] = *ptrs[c];
    s_sp[c] = (Spring){ s_neutral[c], 0, s_neutral[c] };
  }
  set_gains();
  memset(s_prop, 0, sizeof(s_prop));
  s_act = ACT_NONE;
  s_frozen = false;
  compose();
}

void anim_freeze(ActId act, int ms) {
  s_idle = false;
  s_next_blink = 1e9f;
  if (act != ACT_NONE) anim_play(act);
  for (int t = 0; t < ms; t += 33) step(0.033f);
  // Hold whatever the act was doing at that instant.
  s_frozen = true;
}
