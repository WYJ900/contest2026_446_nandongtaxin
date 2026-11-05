"""Wake the screen and keep ringing while the impact safety check is open.

The safety check page was silent and did not light a dark screen, so a
rider who fell (or a watch lying face down) gave no sign. note_watch runs
every second even while the screen is dark; on the edge into
VELARIDE_ALERT_IMPACT it wakes the screen, then re-requests the safety cue
(see patch_safety_sound.py; rings through mute/DND) whenever it is idle, so
the alarm continues until the check is answered or the ride ends. A side
key press silences it for the rest of that check.
Waking from code (not touch) also drops the wake shield, as the side key
does; otherwise the first tap on the check page was swallowed.
Usage: python patch_safety_alert_cue.py <velaride_shell.c>
"""
import sys

path = sys.argv[1]
s = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def rep(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('pattern count %d: %s' % (s.count(old), old[:70]))
    s = s.replace(old, new)


rep('''  bool connected;
  bool rest_posted;''', '''  bool connected;
  bool rest_posted;
  bool impact_alerted;
  bool impact_silenced;''')

rep('''  else if (ride.alert != VELARIDE_ALERT_REST)
    {
      g_notes.rest_posted = false;
    }

  g_notes.watch_started = true;''', '''  else if (ride.alert != VELARIDE_ALERT_REST)
    {
      g_notes.rest_posted = false;
    }

  if (ride.alert == VELARIDE_ALERT_IMPACT)
    {
      if (!g_notes.impact_alerted)
        {
          g_notes.impact_alerted = true;
          g_notes.impact_silenced = false;
          screen_wake_untouched();
          printf("VELARIDE_SAFETY: check shown, screen woken\\n");
        }

#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
      if (!g_notes.impact_silenced)
        {
          struct velaride_sound_s sound;

          velaride_sound_get(&sound);
          if (sound.active == VR_SOUND_NONE &&
              sound.pending == VR_SOUND_NONE)
            {
              velaride_sound_request(VR_SOUND_SAFETY);
            }
        }
#endif
    }
  else if (g_notes.impact_alerted)
    {
      g_notes.impact_alerted = false;
      g_notes.impact_silenced = false;
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
      velaride_sound_stop(VR_SOUND_SAFETY);
#endif
      printf("VELARIDE_SAFETY: check closed\\n");
    }

  g_notes.watch_started = true;''')

rep('''  g_menu.button_has_action = true;
  g_menu.button_action_us = now;
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop_alerts();
#endif''', '''  g_menu.button_has_action = true;
  g_menu.button_action_us = now;
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop_alerts();
#endif
  if (g_notes.impact_alerted)
    {
      g_notes.impact_silenced = true;
    }
''')


rep('''static void screen_wake(bool raise);
''', '''static void screen_wake(bool raise);
static void screen_wake_untouched(void);
''')

rep('''static void shield_cb(lv_event_t *event)
{''', '''/* Wake for an event, not a touch: nothing is pressed, so the first tap
 * must reach the page instead of the wake shield.
 */

static void screen_wake_untouched(void)
{
  screen_wake(false);
  lv_obj_add_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN);
}

static void shield_cb(lv_event_t *event)
{''')

open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('patched', path)
