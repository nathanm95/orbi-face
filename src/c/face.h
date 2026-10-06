#pragma once
#include <pebble.h>

// What the face looks like this frame. anim.c springs every float here towards
// its expression's targets; face.c only draws it. Units are noted per field.

typedef enum { EYES_NORMAL = 0, EYES_HEART, EYES_X, EYES_SPIN, EYES_COUNT } EyeMode;

typedef enum {
  PROP_NONE = 0,
  PROP_SUNGLASSES,
  PROP_BEANIE,
  PROP_UMBRELLA,
  PROP_PARTYHAT,
  PROP_WITCHHAT,
  PROP_SANTAHAT,
  PROP_NIGHTCAP,
  PROP_CLOUD,
  PROP_COUNT
} PropId;

// Which part of the face a prop sits on. One prop per slot.
typedef enum { SLOT_HEAD = 0, SLOT_EYES, SLOT_SKY, SLOT_COUNT } PropSlot;

typedef enum {
  P_NONE = 0,
  P_RAIN,
  P_SNOW,
  P_HEART,
  P_SPARKLE,
  P_CONFETTI,
  P_ZZZ,
  P_TEAR,
  P_SWEAT,
  P_STEAM,
  P_NOTE,
  P_STAR,
  P_LEAF,
  P_SPLASH,
  P_FOG,
  P_BOLT,
  P_SPIT,
} ParticleKind;

typedef struct {
  uint8_t kind;
  uint8_t color;
  uint8_t ambient;      // weather, not an act: may freeze mid-air when idle
  float x, y, vx, vy;   // px, px/s
  float age, life;      // s
  float size, angle, spin;
} Particle;

#define MAX_PARTICLES 40

typedef struct {
  uint8_t id;     // PropId
  float show;     // 0 hidden (off screen) .. 1 in place
  float dx, dy;   // extra offset, px
} PropState;

typedef struct {
  // Head: turning moves every feature over the sphere and foreshortens it.
  float yaw, pitch, roll;   // radians
  float bob;                // px, vertical bounce
  float shake_x, shake_y;   // px, jitter
  float scale;              // whole-face scale, 1 = normal

  // Eyes. Lids: 0 fully open .. 1 shut. Blinks multiply into lid_top.
  float gaze_x, gaze_y;     // -1..1 of the room the pupil has in the eye
  float cross;              // 0..1 cross-eyed
  float pupil;              // pupil size, 1 = normal
  float eye_scale;          // eye size, 1 = normal
  float eye_l, eye_r;       // per-eye extra size (wonky / squinting one eye)
  float lid_top_l, lid_top_r;
  float lid_bot_l, lid_bot_r;
  float lid_tilt;           // + angry (inner corners down), - sad
  float happy;              // bottom lid curvature: 1 = smiling ^^ eyes
  uint8_t eye_mode;         // EyeMode
  float eye_phase;          // radians, drives heart pulse / spinning pupils

  // Brows.
  float brow_y;             // px raise
  float brow_tilt;          // + angry, - worried
  float brow_l, brow_r;     // px extra raise per side (sceptical)
  float brow_arch;          // 0 flat .. 1 arched

  // Mouth.
  float mouth_open;         // 0 closed line .. 1 wide open
  float mouth_w;            // width, 1 = normal
  float mouth_curve;        // -1 frown .. 1 smile
  float mouth_round;        // 0 grin (D) .. 1 round (O)
  float mouth_skew;         // -1..1 tilts it (smirk)
  float mouth_x;            // px shift
  float teeth;              // 0..1
  float tongue;             // 0..1 tongue sticking out

  // Colour.
  float cheek;              // blush 0..1
  float flush;              // anger rising from the chin 0..1
  float sweat;              // sweat drop on the temple 0..1
  float flash;              // lightning flash 0..1

  PropState props[SLOT_COUNT];
  Particle particles[MAX_PARTICLES];
} FaceState;

// The neutral resting face.
void face_neutral(FaceState *f);

// Back pass: orb, features. Rows as for r_begin().
void face_render_back(const FaceState *f, uint8_t **rows, const int16_t *minx,
                      const int16_t *maxx);
// Front pass: particles and the lightning flash, drawn over the props.
void face_render_front(const FaceState *f, uint8_t **rows, const int16_t *minx,
                       const int16_t *maxx);

// Where to draw the prop in `slot` this frame (top-left corner, px), given
// its bitmap size. Returns false for an empty or fully hidden slot.
bool face_prop_pos(const FaceState *f, int slot, int w, int h, int *x, int *y);

// The black pill behind the clock, drawn in the back pass when enabled.
void face_set_pill(bool on);
#define PILL_X 76
#define PILL_Y 212
#define PILL_W 108
#define PILL_H 34
