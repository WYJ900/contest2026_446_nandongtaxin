"""Add a safety-check cue that rings through do-not-disturb and mute.

VR_SOUND_SAFETY reuses the alarm waveform and repetition, but a fall
warning must be audible even if the rider set DND, muted rings or turned
the volume down. It is played at no less than 70 % volume and outranks the
alarm. Every other cue keeps its existing gating.
The shell "sound play" name table gains "safety" so the cue can be checked
from the serial console.
Usage: python patch_safety_sound.py <velaride_sound.h> <velaride_sound.c> <velaride_sound_render.c> <velaride_shell.c>
"""
import sys


def load(path):
    s = open(path, encoding='utf-8', newline='').read()
    return s.replace('\r\n', '\n'), ('\r\n' if '\r\n' in s else '\n')


def rep(s, old, new):
    if s.count(old) != 1:
        raise SystemExit('pattern count %d: %s' % (s.count(old), old[:70]))
    return s.replace(old, new)


def save(path, s, nl):
    open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


h_path, c_path, r_path, sh_path = sys.argv[1:5]

h, nl = load(h_path)
h = rep(h, '''  VR_SOUND_STOPWATCH_MODE,
  VR_SOUND_CUE_COUNT''', '''  VR_SOUND_STOPWATCH_MODE,
  VR_SOUND_SAFETY,            /* Fall check: rings through DND and mute. */
  VR_SOUND_CUE_COUNT''')
save(h_path, h, nl)

c, nl = load(c_path)
c = rep(c, '''  uint64_t deadline;
  bool voice;
};''', '''  uint64_t deadline;
  bool voice;
  bool safety;
};''')
c = rep(c, '''static bool sound_voice(int cue)
{
  return cue > VR_SOUND_NONE && cue < VR_SOUND_CUE_COUNT &&
         cue != VR_SOUND_ALARM && cue != VR_SOUND_COUNTDOWN;
}

static int sound_priority(int cue)
{
  if (cue == VR_SOUND_ALARM) return 100;''', '''#define SOUND_SAFETY_MIN_VOLUME 70

static bool sound_voice(int cue)
{
  return cue > VR_SOUND_NONE && cue < VR_SOUND_CUE_COUNT &&
         cue != VR_SOUND_ALARM && cue != VR_SOUND_COUNTDOWN &&
         cue != VR_SOUND_SAFETY;
}

/* Settings and DND never silence the fall check. */

static bool sound_muted_locked(int cue)
{
  if (cue == VR_SOUND_SAFETY) return false;
  return !g_sound.enabled || g_sound.volume == 0 || g_sound.dnd;
}

static int sound_priority(int cue)
{
  if (cue == VR_SOUND_SAFETY) return 120;
  if (cue == VR_SOUND_ALARM) return 100;''')
c = rep(c, '''  volume = g_sound.volume;
  pthread_mutex_unlock(&g_sound_lock);
  return velaride_sound_gain(volume, play->voice);''', '''  volume = g_sound.volume;
  if (play->safety && volume < SOUND_SAFETY_MIN_VOLUME)
    {
      volume = SOUND_SAFETY_MIN_VOLUME;
    }

  pthread_mutex_unlock(&g_sound_lock);
  return velaride_sound_gain(volume, play->voice);''')
c = rep(c, '''      if (!g_sound.enabled || g_sound.volume == 0 || g_sound.dnd ||
          sound_now() > g_sound_expires)''', '''      if (sound_muted_locked(cue) || sound_now() > g_sound_expires)''')
c = rep(c, '''      play.deadline = sound_now() + (cue == VR_SOUND_ALARM ? 10000 : 3000);
      play.voice = sound_voice(cue);''', '''      play.deadline = sound_now() +
                      (cue == VR_SOUND_ALARM || cue == VR_SOUND_SAFETY ?
                       10000 : 3000);
      play.voice = sound_voice(cue);
      play.safety = cue == VR_SOUND_SAFETY;''')
c = rep(c, '''      repeat = cue == VR_SOUND_ALARM ? 4 : 1;''',
        '''      repeat = cue == VR_SOUND_ALARM || cue == VR_SOUND_SAFETY ? 4 : 1;''')
c = rep(c, '''  if (!g_sound.enabled || g_sound.volume == 0 || g_sound.dnd ||
      (sound_voice(cue) ? !g_sound.voice : !g_sound.rings))''', '''  if (cue != VR_SOUND_SAFETY &&
      (sound_muted_locked(cue) ||
       (sound_voice(cue) ? !g_sound.voice : !g_sound.rings)))''')
save(c_path, c, nl)

r, nl = load(r_path)
r = rep(r, '''  if (cue == VR_SOUND_ALARM || cue == VR_SOUND_COUNTDOWN)
    {
      count = cue == VR_SOUND_ALARM ? 32000 : 14400;''', '''  if (cue == VR_SOUND_SAFETY)
    {
      cue = VR_SOUND_ALARM;
    }

  if (cue == VR_SOUND_ALARM || cue == VR_SOUND_COUNTDOWN)
    {
      count = cue == VR_SOUND_ALARM ? 32000 : 14400;''')
save(r_path, r, nl)
sh, nl = load(sh_path)
sh = rep(sh, '''            "go", "alarm", "countdown", "alarm-mode", "countdown-mode",
            "stopwatch"};''', '''            "go", "alarm", "countdown", "alarm-mode", "countdown-mode",
            "stopwatch", "safety"};''')
save(sh_path, sh, nl)
print('patched sound')
