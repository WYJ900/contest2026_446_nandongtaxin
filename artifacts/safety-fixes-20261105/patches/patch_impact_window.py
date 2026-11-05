"""Measure the post-impact stillness window from the last jolt, not the first.

Field log 11-05 20:35-20:38: a shake or tumble lasting longer than the
10 s window armed impact_pending on its first jolt; later jolts were
ignored while pending, so by the time the watch lay still the window had
expired and only the 15 s "still" prompt appeared. Every jolt now restarts
the window; impact_count still counts one per episode.
Usage: python patch_impact_window.py <velaride_ride_session.c>
"""
import sys

path = sys.argv[1]
s = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')
old = '''  if (impact_sample && !g_ride.state.paused && !g_ride.impact_pending)
    {
      g_ride.impact_pending = true;
      g_ride.impact_age_s = 0;
      g_ride.post_impact_still_s = 0;
      g_ride.state.impact_count++;
      printf("VELARIDE_RIDE: impact delta=%d gyro=%d count=%u\\n",
             accel_delta, gyro_sum, g_ride.state.impact_count);
    }
'''
new = '''  if (impact_sample && !g_ride.state.paused)
    {
      if (!g_ride.impact_pending)
        {
          g_ride.state.impact_count++;
          printf("VELARIDE_RIDE: impact delta=%d gyro=%d count=%u\\n",
                 accel_delta, gyro_sum, g_ride.state.impact_count);
        }

      g_ride.impact_pending = true;
      g_ride.impact_age_s = 0;
      g_ride.post_impact_still_s = 0;
    }
'''
if s.count(old) != 1:
    raise SystemExit('pattern count %d' % s.count(old))
open(path, 'w', encoding='utf-8', newline='').write(s.replace(old, new).replace('\n', nl))
print('patched', path)
