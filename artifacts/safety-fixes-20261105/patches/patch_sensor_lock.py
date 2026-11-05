"""Serialize every LSM6DSL access in velaride_ride_session.c.

The NuttX lsm6dsl driver has no lock: SNIOC_LSM6DSLSENSORREAD is several
I2C register reads. Since the low-activity reminder samples from the device
worker thread while the UI thread polls raise-to-wake every 50 ms, two reads
can interleave and return torn samples, which flips the raise/away pose.
Usage: python patch_sensor_lock.py <velaride_ride_session.c>
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


rep('''static int ride_open_sensor(void)
{
  int ret;

  if (g_ride.sensor_fd >= 0)
    {
      return 0;
    }
''', '''/* The lsm6dsl driver does not serialize callers; the UI thread (ride and
 * raise-to-wake) and the device worker (low-activity reminder) share it. */

static pthread_mutex_t g_sensor_lock = PTHREAD_MUTEX_INITIALIZER;

static int ride_open_sensor_locked(void)
{
  int ret;

  if (g_ride.sensor_fd >= 0)
    {
      return 0;
    }
''')

rep('''      close(g_ride.sensor_fd);
      g_ride.sensor_fd = -1;
      return -errno;
    }

  return 0;
}
''', '''      close(g_ride.sensor_fd);
      g_ride.sensor_fd = -1;
      return -errno;
    }

  return 0;
}

static int ride_open_sensor(void)
{
  int ret;

  pthread_mutex_lock(&g_sensor_lock);
  ret = ride_open_sensor_locked();
  pthread_mutex_unlock(&g_sensor_lock);
  return ret;
}

static int ride_sensor_read(struct lsm6dsl_sensor_data_s *data)
{
  int ret;

  pthread_mutex_lock(&g_sensor_lock);
  ret = ride_open_sensor_locked();
  if (ret == 0 && ioctl(g_ride.sensor_fd, SNIOC_LSM6DSLSENSORREAD,
                        (unsigned long)data) < 0)
    {
      ret = -errno;
    }

  pthread_mutex_unlock(&g_sensor_lock);
  return ret;
}
''')

rep('''  ret = ride_open_sensor();
  if (ret == 0 && ioctl(g_ride.sensor_fd, SNIOC_LSM6DSLSENSORREAD,
                        (unsigned long)&data) < 0)
    {
      ret = -errno;
    }

  g_ride.state.sensor_ready = ret == 0;''', '''  ret = ride_sensor_read(&data);
  g_ride.state.sensor_ready = ret == 0;''')

rep('''  struct lsm6dsl_sensor_data_s data;
  int ret = ride_open_sensor();

  if (ret < 0)
    {
      return ret;
    }

  if (ioctl(g_ride.sensor_fd, SNIOC_LSM6DSLSENSORREAD,
            (unsigned long)&data) < 0)
    {
      return -errno;
    }

  *x = data.x_data;''', '''  struct lsm6dsl_sensor_data_s data;
  int ret = ride_sensor_read(&data);

  if (ret < 0)
    {
      return ret;
    }

  *x = data.x_data;''')

rep('''  ret = ioctl(g_ride.sensor_fd, SNIOC_LSM6DSLSENSORREAD,
              (unsigned long)&data);
  if (ret < 0)
    {
      g_ride.state.sensor_ready = false;''', '''  ret = ride_sensor_read(&data);
  if (ret < 0)
    {
      g_ride.state.sensor_ready = false;''')

if 'SNIOC_LSM6DSLSENSORREAD' in s.split('static int ride_sensor_read', 1)[1].split('pthread_mutex_unlock(&g_sensor_lock);\n  return ret;\n}', 1)[1]:
    raise SystemExit('unlocked sensor read remains')
open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('patched', path)
