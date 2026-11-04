#define open fake_open
#define close fake_close
#define read fake_read
#define ioctl fake_ioctl
#define clock_gettime fake_clock_gettime
#include "../../app/velaride/velaride_device.c"
#undef open
#undef close
#undef read
#undef ioctl
#undef clock_gettime

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

static uint32_t fake_now = 1000;
static struct adc_msg_s adc_sample = {7, 4200};
static int trigger_error;
static ssize_t read_size = sizeof(struct adc_msg_s);
static bool valid_ids = true;
static struct sensor_light light_sample = {1000, 100, 0};
static int light_error;
static int save_error;
static uint8_t disk[8];
static int commits;
static bool activity_enabled,activity_riding,activity_sample_valid;
static int activity_reads;
void velaride_activity_get(struct velaride_activity_s *out) {memset(out,0,sizeof(*out));out->enabled=activity_enabled;}
void velaride_activity_sample(uint32_t now,unsigned int minute,bool time_valid,bool riding,bool valid,int16_t x,int16_t y,int16_t z)
{(void)now;(void)minute;(void)time_valid;(void)riding;(void)x;(void)y;(void)z;activity_sample_valid=valid;}
void velaride_ride_session_get(struct velaride_ride_snapshot_s *out) {memset(out,0,sizeof(*out));out->active=activity_riding;}
int velaride_ride_session_read_accel(int16_t *x,int16_t *y,int16_t *z) {*x=0;*y=0;*z=16384;activity_reads++;return 0;}
void velaride_compass_init(void) {}
void velaride_compass_sample(void) {}

ssize_t property_get_buffer(const char *key, void *data, size_t size)
{
  (void)key;
  assert(size == sizeof(disk));
  memcpy(data, disk, size);
  return size;
}

int property_set_buffer(const char *key, const void *data, size_t size)
{
  (void)key;
  assert(size == sizeof(disk));
  memcpy(disk, data, size);
  return save_error;
}

int property_commit(void) { commits++; return save_error; }

int fake_clock_gettime(clockid_t id, struct timespec *out)
{
  (void)id;
  out->tv_sec = fake_now / 1000;
  out->tv_nsec = (fake_now % 1000) * 1000000L;
  return 0;
}

int fake_open(const char *path, int flags, ...)
{
  (void)flags;
  if (strcmp(path, "/dev/i2c1") == 0) return 5;
  if (strcmp(path, "/dev/gpio3") == 0) return 3;
  if (strcmp(path, "/dev/uorb/sensor_light0") == 0) return 6;
  assert(strcmp(path, "/dev/adc0") == 0);
  return 4;
}

int fake_close(int fd) { (void)fd; return 0; }

ssize_t fake_read(int fd, void *buffer, size_t size)
{
  if (fd == 6)
    {
      assert(size == sizeof(light_sample));
      if (light_error) { errno = light_error; return -1; }
      memcpy(buffer, &light_sample, size);
      return size;
    }
  assert(fd == 4 && size == sizeof(adc_sample));
  memcpy(buffer, &adc_sample, sizeof(adc_sample));
  return read_size;
}

int fake_ioctl(int fd, unsigned long cmd, ...)
{
  va_list args;
  va_start(args, cmd);
  if (cmd == I2CIOC_TRANSFER)
    {
      struct i2c_transfer_s *t = (void *)va_arg(args, unsigned long);
      assert(fd == 5 && t->msgc == 2);
      assert(t->msgv[0].flags == I2C_M_NOSTOP);
      assert(t->msgv[1].flags == I2C_M_READ);
      uint8_t reg = *t->msgv[0].buffer;
      *t->msgv[1].buffer = !valid_ids ? 0 :
        reg == 0x86 ? 0xa0 : reg == 0x87 ? 0x05 : 0x10;
    }
  else if (cmd == GPIOC_READ)
    {
      assert(fd == 3);
      *(bool *)va_arg(args, unsigned long) = true;
    }
  else
    {
      assert(fd == 4);
      if (cmd == ANIOC_TRIGGER && trigger_error)
        {
          va_end(args);
          errno = trigger_error;
          return -1;
        }
    }
  va_end(args);
  return 0;
}

int main(void)
{
  struct velaride_device_snapshot_s s = {0};
  sample_voltage(4, &s);
  assert(s.adc_valid && s.reference_mv == 4200 && s.adc_error == 0);
  trigger_error = ETIMEDOUT;
  sample_voltage(4, &s);
  assert(!s.adc_valid && s.adc_error == -ETIMEDOUT);
  trigger_error = 0;
  read_size = 1;
  sample_voltage(4, &s);
  assert(!s.adc_valid && s.adc_error == -EIO);
  read_size = sizeof(adc_sample);
  adc_sample.am_channel = 5;
  sample_voltage(4, &s);
  assert(!s.adc_valid && s.adc_error == -ERANGE);
  adc_sample.am_channel = 7;
  adc_sample.am_data = 0;
  sample_voltage(4, &s);
  assert(!s.adc_valid && s.adc_error == -ERANGE);
  adc_sample.am_data = 4200;
  sample_voltage(4, &s);
  probe_ids(&s);
  assert(s.adc_samples == 6 && s.adc_failures == 4);
  assert(s.light_detected && s.magnetometer_detected);
  valid_ids = false;
  probe_ids(&s);
  assert(!s.light_detected && !s.magnetometer_detected);
  g_device = s;
  g_device.usb_valid = true;
  g_device.usb_connected = true;
  g_device.usb_sample_ms = fake_now;
  velaride_device_get(&s);
  assert(s.adc_valid && s.usb_valid);
  fake_now += 31000;
  velaride_device_get(&s);
  assert(!s.adc_valid && !s.usb_valid && s.adc_error == -ETIMEDOUT);
  fake_now = 60000;  /* Reader starts long after boot. */
  light_error = EAGAIN;
  assert(open_light(&s) == 6);
  sample_light(6, &s);
  assert(!s.light_valid && s.light_error == -EAGAIN);
  fake_now += 1000;
  light_error = 0;
  sample_light(6, &s);
  assert(s.light_valid && s.light_lux == 100);
  light_error = EAGAIN;
  sample_light(6, &s);
  assert(s.light_valid);
  fake_now += 4000;
  sample_light(6, &s);
  assert(!s.light_valid && s.light_error == -ETIMEDOUT);
  light_error = EIO;
  sample_light(6, &s);
  assert(!s.light_valid && s.light_error == -EIO);
  light_error = 0;
  light_sample.light = NAN;
  sample_light(6, &s);
  assert(!s.light_valid);
  light_sample.light = 0;
  sample_light(6, &s);
  assert(s.light_valid && s.light_lux == 0);
  g_device = s;
  assert(velaride_display_set(0, false) == -EINVAL);
  assert(velaride_display_set(100, false) == -EINVAL);
  velaride_display_set(20, false);
  assert(velaride_display_tick() == 20);
  velaride_display_applied(20, 0);
  velaride_display_applied(80, -EIO);
  assert(g_display.applied == 20 && g_display.apply_error == -EIO);
  display_save();
  assert(!g_display.save_pending && commits == 1);
  display_save();
  assert(commits == 1);
  g_display.manual = 50;
  display_load();
  assert(g_display.manual == 20);
  disk[4] = 0;
  display_load();
  assert(g_display.manual == 20);
  save_error = -EIO;
  velaride_display_set(50, false);
  display_save();
  assert(g_display.save_error == -EIO);
  save_error = 0;
  velaride_display_set(50, true);
  display_save();
  assert(g_display.save_error == 0);
  g_display.target = 50;
  uint8_t prev = velaride_display_tick();
  for (int i = 0; i < 20; i++)
    {
      fake_now += 250;
      g_device.light_sample_ms = fake_now;
      uint8_t next = velaride_display_tick();
      assert(abs((int)next - prev) <= 6);
      prev = next;
    }
  assert(prev == 25);
  g_device.light_lux = 1000;
  for (int i = 0; i < 8; i++)
    {
      fake_now += 250;
      g_device.light_sample_ms = fake_now;
      uint8_t next = velaride_display_tick();
      assert(abs((int)next - prev) <= 15 && next >= 25 && next <= 80);
      prev = next;
    }
  assert(prev >= 77); /* Bright environment reached within two seconds. */
  g_device.light_lux = 300;
  for (int i = 0; i < 20; i++)
    {
      fake_now += 250; g_device.light_sample_ms = fake_now;
      prev = velaride_display_tick();
    }
  assert(prev >= 64 && prev <= 69);
  uint8_t steady = prev;
  for (int i = 0; i < 20; i++)
    {
      fake_now += 250; g_device.light_sample_ms = fake_now;
      g_device.light_lux = i % 2 ? 295 : 305;
      assert(velaride_display_tick() == steady);
    }
  g_device.light_lux = 0;
  for (int i = 0; i < 20; i++)
    {
      fake_now += 250; g_device.light_sample_ms = fake_now;
      uint8_t next = velaride_display_tick();
      assert((int)prev - next <= 6); prev = next;
    }
  assert(prev <= 28);
  fake_now += 4000;
  assert(velaride_display_tick() == 50); /* Stale light cannot drive brightness. */
  g_device.light_valid = false;
  assert(velaride_display_tick() == 50);
  velaride_display_set(20, false);
  assert(velaride_display_tick() == 20);
  {
    struct tm noon={.tm_year=126,.tm_mon=10,.tm_mday=4,.tm_hour=12};
    time_t day=mktime(&noon);
    activity_enabled=true;sample_activity(fake_now,day);assert(activity_reads==1&&activity_sample_valid);
    activity_riding=true;sample_activity(fake_now,day);assert(activity_reads==1&&!activity_sample_valid);
    activity_riding=false;activity_enabled=false;sample_activity(fake_now,day);assert(activity_reads==1&&!activity_sample_valid);
    activity_enabled=true;noon.tm_hour=23;sample_activity(fake_now,mktime(&noon));assert(activity_reads==1&&!activity_sample_valid);
    sample_activity(fake_now,0);assert(activity_reads==1&&!activity_sample_valid);
  }
  puts("PASS: ADC/light/brightness regressions and sedentary sampling disabled during ride, quiet hours, disabled setting and invalid time");
  return 0;
}
