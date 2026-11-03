#define open fake_open
#define close fake_close
#define read fake_read
#define clock_gettime fake_clock_gettime
#include "../../app/velaride/velaride_compass.c"
#undef open
#undef close
#undef read
#undef clock_gettime
#include <assert.h>
#include <stdio.h>

static uint64_t now_us = 60000000;
static struct compass_cal_s stored;
static int save_error;
static int read_error;
static int closed;
static struct sensor_mag reading = {.x = 30, .y = 20, .z = 10};
int fake_clock_gettime(clockid_t id, struct timespec *out)
{
  (void)id; out->tv_sec = now_us / 1000000; out->tv_nsec = now_us % 1000000 * 1000; return 0;
}
int fake_open(const char *path, int flags, ...)
{ assert(strcmp(path,"/dev/uorb/sensor_mag0")==0); assert(flags & O_NONBLOCK); return 7; }
int fake_close(int fd) { assert(fd == 7); closed++; return 0; }
ssize_t fake_read(int fd, void *out, size_t size)
{
  assert(fd == 7 && size == sizeof(reading));
  if (read_error) { errno = read_error; return -1; }
  memcpy(out, &reading, size); return size;
}
ssize_t property_get_buffer(const char *key, void *data, size_t size)
{ assert(strcmp(key,COMPASS_KEY)==0 && size == sizeof(stored)); memcpy(data,&stored,size); return size; }
int property_set_buffer(const char *key, const void *data, size_t size)
{ assert(strcmp(key,COMPASS_KEY)==0 && size == sizeof(stored)); memcpy(&stored,data,size); return save_error; }
int property_commit(void) { return save_error; }

static void rotate_sphere(void)
{
  for (int n = 0; n < 160; n++)
    {
      float z = 1.0f - 2.0f * (n + 0.5f) / 160;
      float radius = sqrtf(1-z*z);
      float angle = n * 2.39996323f;
      struct sensor_mag mag = {.x = 10+60*radius*cosf(angle),
        .y = -5+40*radius*sinf(angle), .z = 8+50*z};
      now_us += 250000;
      compass_accept(&mag);
    }
}

int main(void)
{
  struct velaride_compass_s state;
  velaride_compass_init();
  assert(!g_compass.calibrated);
  velaride_compass_enable(true);
  velaride_compass_sample();
  velaride_compass_get(&state);
  assert(state.valid && !state.heading_valid && state.samples == 1);
  read_error = EIO;
  velaride_compass_sample();
  assert(!g_compass.valid && g_compass.error == -EIO && closed == 1);
  read_error = 0;
  velaride_compass_sample();
  assert(g_compass.valid);
  now_us += 2000000;
  velaride_compass_get(&state);
  assert(!state.valid && state.error == -ETIMEDOUT);
  velaride_compass_calibrate();
  assert(velaride_compass_finish() == -EAGAIN);
  for (int i = 0; i < 100; i++) compass_accept(&reading);
  assert(velaride_compass_finish() == -EAGAIN); /* Stationary cannot calibrate. */
  velaride_compass_cancel();
  assert(!g_compass.calibrating && !g_compass.calibrated);
  velaride_compass_calibrate();
  rotate_sphere();
  assert(velaride_compass_finish() == 0 && g_compass.calibrated);
  assert(fabsf(g_cal.bias[0] - 10) < 2 && fabsf(g_cal.bias[1] + 5) < 2);
  save_error = -EIO;
  velaride_compass_sample();
  assert(g_compass.save_error == -EIO && !g_compass.save_pending);
  save_error = 0;
  velaride_compass_calibrate(); rotate_sphere();
  assert(velaride_compass_finish() == 0);
  velaride_compass_sample();
  assert(g_compass.save_error == 0 && stored.checksum == calibration_checksum(&stored));
  g_compass.calibrated = false;
  velaride_compass_init();
  assert(g_compass.calibrated);
  stored.checksum++;
  g_compass.calibrated = false;
  velaride_compass_init();
  assert(!g_compass.calibrated);
  g_compass.calibrated = true;
  struct sensor_mag mag = {.x = g_cal.bias[0]+50/g_cal.scale[0],
    .y = g_cal.bias[1], .z = g_cal.bias[2]};
  compass_accept(&mag);
  assert(g_compass.heading_valid && fabsf(g_compass.heading) < 0.01f);
  mag.x = g_cal.bias[0]; mag.y = g_cal.bias[1]-50/g_cal.scale[1];
  compass_accept(&mag);
  assert(g_compass.heading_valid && fabsf(g_compass.heading-90) < 0.01f);
  mag.x = 1000; compass_accept(&mag);
  assert(!g_compass.heading_valid);
  velaride_compass_calibrate(); rotate_sphere();
  for (int i = 0; i < 100; i++) compass_accept(&reading);
  assert(g_compass.collected == CAL_SAMPLES);
  velaride_compass_enable(false);
  velaride_compass_sample();
  assert(!g_compass.valid && !g_compass.calibrating && closed == 2);
  puts("PASS: compass sample errors/staleness, bounded calibration, stationary rejection, bias/scale, persistence, field gating and bearing math");
}
