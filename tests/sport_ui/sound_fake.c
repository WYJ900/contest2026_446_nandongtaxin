#include "velaride_sound.h"
#include <string.h>
static struct velaride_sound_s prefs = {.voice = true, .rings = true, .volume = 50};
int sound_test_events[64];
int sound_test_count;
int sound_test_voice_stops;
int sound_test_alert_stops;
void sound_test_reset(void)
{
  sound_test_count = sound_test_voice_stops = sound_test_alert_stops = 0;
  memset(sound_test_events, 0, sizeof(sound_test_events));
}
void velaride_sound_get(struct velaride_sound_s *out) { *out = prefs; }
int velaride_sound_set(bool enabled, bool voice, bool rings, uint8_t volume)
{
  prefs.enabled = enabled; prefs.voice = voice; prefs.rings = rings; prefs.volume = volume;
  return 0;
}
int velaride_sound_request(enum velaride_sound_cue_e cue)
{
  if (sound_test_count < 64) sound_test_events[sound_test_count++] = cue;
  return 0;
}
int velaride_sound_set_volume(uint8_t volume) { prefs.volume = volume; return 0; }
void velaride_sound_stop(enum velaride_sound_cue_e cue) { (void)cue; prefs.active = 0; }
void velaride_sound_stop_voice(void) { sound_test_voice_stops++; }
void velaride_sound_stop_alerts(void) { sound_test_alert_stops++; }
void velaride_sound_set_dnd(bool enabled) { prefs.dnd = enabled; }
