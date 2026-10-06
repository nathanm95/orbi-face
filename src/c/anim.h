#pragma once
#include <pebble.h>
#include "face.h"

// Orbi's behaviour: springs that carry the face between expressions, scripted
// "acts" (laughing, sneezing, putting sunglasses on...), idle life between
// them, and weather that falls on him. Portable: no SDK calls beyond trig, so
// it also runs in the host harness.

typedef enum {
  ACT_NONE = 0,
  // Everyday, picked at random.
  ACT_GLANCE,
  ACT_LOOK_AROUND,
  ACT_LAUGH,
  ACT_WINK,
  ACT_CHEEKY,
  ACT_SURPRISED,
  ACT_YAWN,
  ACT_LOVE,
  ACT_DIZZY,
  ACT_COOL,
  ACT_GRUMPY,
  ACT_SNEEZE,
  ACT_KISS,
  ACT_EYEROLL,
  ACT_CRY,
  ACT_WHISTLE,
  ACT_SILLY,
  ACT_HICCUP,
  ACT_SUSPICIOUS,
  ACT_PARTY,
  // Weather.
  ACT_RAIN,
  ACT_SNOW,
  ACT_COLD,
  ACT_HOT,
  ACT_SUNNY,
  ACT_THUNDER,
  ACT_FOG,
  ACT_WIND,
  ACT_CLOUDY,
  ACT_NIGHT,
  // Seasons and events.
  ACT_HALLOWEEN,
  ACT_XMAS,
  ACT_CHARGING,
  ACT_LOW_BATTERY,
  ACT_BT_LOST,
  ACT_SLEEP,
  ACT_WAKE,
  ACT_POKED,
  ACT_COUNT
} ActId;

#define ACT_FIRST_FUN ACT_GLANCE
#define ACT_LAST_FUN ACT_PARTY

// What Orbi's resting face is between acts.
typedef enum { MOOD_NORMAL = 0, MOOD_SLEEPY, MOOD_ASLEEP, MOOD_TIRED, MOOD_WORRIED } Mood;

// Weather as the watch knows it (from the phone). Matches src/pkjs/index.js.
typedef enum {
  WX_UNKNOWN = 0,
  WX_CLEAR,
  WX_PARTLY,
  WX_CLOUDY,
  WX_FOG,
  WX_DRIZZLE,
  WX_RAIN,
  WX_SNOW,
  WX_THUNDER,
} WeatherKind;

typedef struct {
  uint8_t kind;   // WeatherKind
  int8_t temp_c;
  bool is_day;
  uint8_t wind_kmh;
} Weather;

void anim_init(void);
// Advances everything by dt seconds.
void anim_step(float dt);
// True while anything still needs frames; when false the face is still and
// the frame timer can stop. Weather particles don't count: they just freeze.
bool anim_busy(void);
const FaceState *anim_face(void);

void anim_play(ActId act);
bool anim_playing(void);
ActId anim_current(void);
// The act that suits right now: weather, season, hour, or a random fun one.
ActId anim_pick(const Weather *wx, int month, int mday, bool weather_on, bool seasonal_on,
                bool props_on);

// Glances and blinks between acts.
void anim_set_idle(bool on);
void anim_blink(void);
void anim_set_mood(Mood mood);
// Weather particles and the resting face's weather tint.
void anim_set_weather(const Weather *wx, bool on);
// Whether weather particles keep spawning (only while frames are running).
void anim_set_ambient(bool on);
void anim_set_props(bool on);

// The weather act that suits this weather.
ActId anim_weather_act(const Weather *wx);
// Takes every prop off.
void anim_clear_props(void);
// Runs time forward until nothing moves: for when frames aren't allowed but
// the face must still change (battery saver, a setting).
void anim_settle(void);

// Debug/store builds: jump an act to `ms` and hold it there.
void anim_freeze(ActId act, int ms);
