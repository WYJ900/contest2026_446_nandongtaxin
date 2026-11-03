/****************************************************************************
 * apps/examples/velaride/velaride_device.c
 *
 * Read-only device telemetry. Sampling never runs in the LVGL thread.
 * ADC calibration is not verified: report reference mV, never battery %.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/ioexpander/gpio.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/uorb.h>
#include <kvdb.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "velaride_device.h"
#include "velaride_compass.h"

#define DEVICE_ADC_PERIOD_MS 10000
#define DEVICE_ADC_CHANNEL 7

static pthread_mutex_t g_device_lock = PTHREAD_MUTEX_INITIALIZER;
static struct velaride_device_snapshot_s g_device;
static bool g_device_started;
static struct velaride_display_s g_display = {.manual = 50, .target = 50};
static uint32_t g_display_generation;
static uint32_t g_display_saved;
static float g_filtered_lux;
static bool g_filter_valid;
static uint32_t g_light_filter_ms;
static uint8_t g_dark_count;
static uint8_t g_auto_desired = 50;

#define DISPLAY_KEY "persist.velaride.display"

static void display_load(void)
{
  uint8_t disk[8];

  if (property_get_buffer(DISPLAY_KEY, disk, sizeof(disk)) == sizeof(disk) &&
      memcmp(disk, "VRD1", 4) == 0 && disk[4] >= 20 && disk[4] <= 80 &&
      disk[5] <= 1 && (uint8_t)(disk[4] ^ disk[6]) == 255 &&
      (uint8_t)(disk[5] ^ disk[7]) == 255)
    {
      g_display.manual = disk[4];
      g_display.automatic = disk[5] != 0;
      g_display.target = disk[4];
    }
}

static void display_save(void)
{
  uint8_t disk[8] = {'V', 'R', 'D', '1', 0, 0, 0, 0};
  uint32_t generation;
  int ret;

  pthread_mutex_lock(&g_device_lock);
  generation = g_display_generation;
  if (generation == g_display_saved)
    {
      pthread_mutex_unlock(&g_device_lock);
      return;
    }

  disk[4] = g_display.manual;
  disk[5] = g_display.automatic;
  pthread_mutex_unlock(&g_device_lock);
  disk[6] = disk[4] ^ 255;
  disk[7] = disk[5] ^ 255;
  ret = property_set_buffer(DISPLAY_KEY, disk, sizeof(disk));
  if (ret >= 0)
    {
      ret = property_commit();
    }

  pthread_mutex_lock(&g_device_lock);
  /* Attempt once per user change; do not wear flash with an error loop. */
  g_display_saved = generation;
  if (generation == g_display_generation)
    {
      g_display.save_pending = false;
      g_display.save_error = ret < 0 ? ret : 0;
    }
  pthread_mutex_unlock(&g_device_lock);
}

#define UI_PREFS_KEY "persist.velaride.ui"

static struct velaride_ui_prefs_s g_ui_prefs;
static uint32_t g_ui_generation;
static uint32_t g_ui_saved;

static void ui_prefs_load(void)
{
  uint8_t disk[10];

  if (property_get_buffer(UI_PREFS_KEY, disk, sizeof(disk)) == sizeof(disk) &&
      memcmp(disk, "VRU1", 4) == 0 &&
      disk[4] < VELARIDE_ACCENT_COUNT && disk[5] < VELARIDE_MENU_COUNT &&
      disk[6] < VELARIDE_TRANSITION_COUNT &&
      (uint8_t)(disk[4] ^ disk[7]) == 255 &&
      (uint8_t)(disk[5] ^ disk[8]) == 255 &&
      (uint8_t)(disk[6] ^ disk[9]) == 255)
    {
      g_ui_prefs.accent = disk[4];
      g_ui_prefs.menu = disk[5];
      g_ui_prefs.transition = disk[6];
    }
}

static void ui_prefs_save(void)
{
  uint8_t disk[10] = {'V', 'R', 'U', '1'};
  uint32_t generation;
  int ret;

  pthread_mutex_lock(&g_device_lock);
  generation = g_ui_generation;
  if (generation == g_ui_saved)
    {
      pthread_mutex_unlock(&g_device_lock);
      return;
    }

  disk[4] = g_ui_prefs.accent;
  disk[5] = g_ui_prefs.menu;
  disk[6] = g_ui_prefs.transition;
  pthread_mutex_unlock(&g_device_lock);
  disk[7] = disk[4] ^ 255;
  disk[8] = disk[5] ^ 255;
  disk[9] = disk[6] ^ 255;
  ret = property_set_buffer(UI_PREFS_KEY, disk, sizeof(disk));
  if (ret >= 0)
    {
      ret = property_commit();
    }

  pthread_mutex_lock(&g_device_lock);
  /* Same policy as the display settings: one attempt per user change. */
  g_ui_saved = generation;
  if (generation == g_ui_generation)
    {
      g_ui_prefs.save_pending = false;
      g_ui_prefs.save_error = ret < 0 ? ret : 0;
    }
  pthread_mutex_unlock(&g_device_lock);
}

#define WAKE_PREFS_KEY "persist.velaride.wake"

/* Raise-to-wake on, screen off after 20 s, until the wearer changes it. */

static struct velaride_wake_prefs_s g_wake_prefs =
{
  .raise = true,
  .sleep = VELARIDE_SLEEP_20S,
};

static uint32_t g_wake_generation;
static uint32_t g_wake_saved;

static void wake_prefs_load(void)
{
  uint8_t disk[8];

  if (property_get_buffer(WAKE_PREFS_KEY, disk, sizeof(disk)) ==
        sizeof(disk) &&
      memcmp(disk, "VRW1", 4) == 0 && disk[4] <= 1 &&
      disk[5] < VELARIDE_SLEEP_COUNT &&
      (uint8_t)(disk[4] ^ disk[6]) == 255 &&
      (uint8_t)(disk[5] ^ disk[7]) == 255)
    {
      g_wake_prefs.raise = disk[4] != 0;
      g_wake_prefs.sleep = disk[5];
    }
}

static void wake_prefs_save(void)
{
  uint8_t disk[8] = {'V', 'R', 'W', '1'};
  uint32_t generation;
  int ret;

  pthread_mutex_lock(&g_device_lock);
  generation = g_wake_generation;
  if (generation == g_wake_saved)
    {
      pthread_mutex_unlock(&g_device_lock);
      return;
    }

  disk[4] = g_wake_prefs.raise ? 1 : 0;
  disk[5] = g_wake_prefs.sleep;
  pthread_mutex_unlock(&g_device_lock);
  disk[6] = disk[4] ^ 255;
  disk[7] = disk[5] ^ 255;
  ret = property_set_buffer(WAKE_PREFS_KEY, disk, sizeof(disk));
  if (ret >= 0)
    {
      ret = property_commit();
    }

  pthread_mutex_lock(&g_device_lock);
  g_wake_saved = generation;
  if (generation == g_wake_generation)
    {
      g_wake_prefs.save_pending = false;
      g_wake_prefs.save_error = ret < 0 ? ret : 0;
    }
  pthread_mutex_unlock(&g_device_lock);
}

static uint32_t device_millis(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static int read_reg(int fd, uint16_t addr, uint8_t reg, uint8_t *value)
{
  struct i2c_msg_s messages[2];
  struct i2c_transfer_s transfer;

  memset(messages, 0, sizeof(messages));
  messages[0].frequency = 100000;
  messages[0].addr = addr;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = &reg;
  messages[0].length = 1;
  messages[1].frequency = 100000;
  messages[1].addr = addr;
  messages[1].flags = I2C_M_READ;
  messages[1].buffer = value;
  messages[1].length = 1;
  transfer.msgv = messages;
  transfer.msgc = 2;
  return ioctl(fd, I2CIOC_TRANSFER, (unsigned long)&transfer);
}

static void probe_ids(struct velaride_device_snapshot_s *sample)
{
  uint8_t part = 0;
  uint8_t maker = 0;
  uint8_t magnetic = 0;
  int fd = open("/dev/i2c1", O_RDWR | O_CLOEXEC);

  sample->light_detected = false;
  sample->magnetometer_detected = false;
  if (fd < 0)
    {
      return;
    }

  if (read_reg(fd, 0x29, 0x86, &part) == 0 &&
      read_reg(fd, 0x29, 0x87, &maker) == 0)
    {
      sample->light_detected = part == 0xa0 && maker == 0x05;
    }

  if (read_reg(fd, 0x30, 0x39, &magnetic) == 0)
    {
      sample->magnetometer_detected = magnetic == 0x10;
    }

  printf("VELARIDE_DEVICE: light_id=%02x maker=%02x magnetic_id=%02x\n",
         part, maker, magnetic);
  close(fd);
}

static void sample_voltage(int fd, struct velaride_device_snapshot_s *sample)
{
  struct adc_msg_s message;
  ssize_t count;

  sample->adc_valid = false;
  sample->adc_samples++;
  sample->adc_sample_ms = device_millis();
  sample->adc_error = 0;
  if (fd < 0)
    {
      sample->adc_error = -ENODEV;
      sample->adc_failures++;
      return;
    }

  if (ioctl(fd, ANIOC_RESET_FIFO, 0) < 0 ||
      ioctl(fd, ANIOC_TRIGGER, 0) < 0)
    {
      sample->adc_error = -errno;
      sample->adc_failures++;
      return;
    }

  count = read(fd, &message, sizeof(message));
  if (count != sizeof(message))
    {
      sample->adc_error = count < 0 ? -errno : -EIO;
      sample->adc_failures++;
      return;
    }

  sample->reference_mv = message.am_data;
  if (message.am_channel != DEVICE_ADC_CHANNEL ||
      message.am_data < 2000 || message.am_data > 5500)
    {
      sample->adc_error = -ERANGE;
      sample->adc_failures++;
      return;
    }

  sample->reference_mv = message.am_data;
  sample->adc_valid = true;
  sample->adc_sample_ms = device_millis();
}

static int open_light(struct velaride_device_snapshot_s *sample)
{
  int fd = open("/dev/uorb/sensor_light0",
                O_RDONLY | O_NONBLOCK | O_CLOEXEC);

  if (fd >= 0)
    {
      /* Start the no-data timeout at open, not at system boot.
       * Activation needs one measurement period before fetch.
       */
      sample->light_valid = false;
      sample->light_error = -EAGAIN;
      sample->light_sample_ms = device_millis();
    }

  return fd;
}

static void sample_light(int fd, struct velaride_device_snapshot_s *sample)
{
  struct sensor_light message;
  ssize_t count;

  if (fd < 0)
    {
      sample->light_valid = false;
      sample->light_error = -ENODEV;
      return;
    }

  count = read(fd, &message, sizeof(message));
  if (count < 0 && errno == EAGAIN)
    {
      if ((uint32_t)(device_millis() - sample->light_sample_ms) > 3000)
        {
          sample->light_valid = false;
          sample->light_error = -ETIMEDOUT;
        }
      return;
    }

  sample->light_valid = count == sizeof(message) &&
                        isfinite(message.light) && message.light >= 0 &&
                        message.light <= 200000;
  if (!sample->light_valid)
    {
      sample->light_error = count < 0 ? -errno : -ERANGE;
      sample->light_failures++;
      return;
    }

  sample->light_error = 0;
  sample->light_lux = message.light;
  sample->light_sample_ms = device_millis();
  sample->light_samples++;
}

static void *device_worker(void *arg)
{
  struct velaride_device_snapshot_s sample;
  uint32_t last_adc = 0;
  uint32_t now;
  bool first = true;
  int adc_fd = -1;
  int usb_fd = -1;
  int light_fd = -1;

  (void)arg;
  memset(&sample, 0, sizeof(sample));
  for (;;)
    {
      now = device_millis();
      if (usb_fd < 0)
        {
          usb_fd = open("/dev/gpio3", O_RDONLY | O_CLOEXEC);
        }

      sample.usb_valid = usb_fd >= 0 &&
        ioctl(usb_fd, GPIOC_READ, (unsigned long)&sample.usb_connected) == 0;
      sample.usb_sample_ms = now;

      if (first || (uint32_t)(now - last_adc) >= DEVICE_ADC_PERIOD_MS)
        {
          if (adc_fd < 0)
            {
              adc_fd = open("/dev/adc0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            }

          sample_voltage(adc_fd, &sample);
          last_adc = now;
        }

      if (first)
        {
          /* Board-mounted parts are probed once, not during later rides. */
          probe_ids(&sample);
        }

      if (light_fd < 0)
        {
          light_fd = open_light(&sample);
        }

      sample_light(light_fd, &sample);
      if (light_fd >= 0 && sample.light_error == -ETIMEDOUT)
        {
          close(light_fd);
          light_fd = -1;
        }

      first = false;
      pthread_mutex_lock(&g_device_lock);
      g_device = sample;
      pthread_mutex_unlock(&g_device_lock);
      display_save();
      ui_prefs_save();
      wake_prefs_save();
      velaride_compass_sample();
      usleep(250000);
    }

  return NULL;
}

int velaride_device_init(void)
{
  pthread_attr_t attr;
  pthread_t thread;
  struct sched_param sched = {.sched_priority = 40};
  int ret;

  pthread_mutex_lock(&g_device_lock);
  if (g_device_started)
    {
      pthread_mutex_unlock(&g_device_lock);
      return 0;
    }

  display_load();
  ui_prefs_load();
  wake_prefs_load();
  velaride_compass_init();
  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      g_device.adc_error = -ret;
      pthread_mutex_unlock(&g_device_lock);
      return -ret;
    }

  if ((ret = pthread_attr_setstacksize(&attr, 8192)) == 0 &&
      (ret = pthread_attr_setschedpolicy(&attr, SCHED_RR)) == 0 &&
      (ret = pthread_attr_setschedparam(&attr, &sched)) == 0 &&
      (ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) == 0 &&
      (ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) == 0)
    {
      ret = pthread_create(&thread, &attr, device_worker, NULL);
    }

  pthread_attr_destroy(&attr);
  if (ret == 0)
    {
      g_device_started = true;
    }
  else
    {
      g_device.adc_error = -ret;
      printf("VELARIDE_DEVICE: worker start failed=%d\n", ret);
    }

  pthread_mutex_unlock(&g_device_lock);
  return -ret;
}

void velaride_device_get(struct velaride_device_snapshot_s *snapshot)
{
  uint32_t now = device_millis();

  if (snapshot == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_device_lock);
  *snapshot = g_device;
  pthread_mutex_unlock(&g_device_lock);
  if ((uint32_t)(now - snapshot->adc_sample_ms) > 30000)
    {
      snapshot->adc_valid = false;
      snapshot->adc_error = -ETIMEDOUT;
    }

  if ((uint32_t)(now - snapshot->usb_sample_ms) > 5000)
    {
      snapshot->usb_valid = false;
    }

  if ((uint32_t)(now - snapshot->light_sample_ms) > 3000)
    {
      snapshot->light_valid = false;
      snapshot->light_error = -ETIMEDOUT;
    }
}

void velaride_display_get(struct velaride_display_s *snapshot)
{
  if (snapshot != NULL)
    {
      pthread_mutex_lock(&g_device_lock);
      *snapshot = g_display;
      pthread_mutex_unlock(&g_device_lock);
    }
}

void velaride_wake_prefs_get(struct velaride_wake_prefs_s *snapshot)
{
  if (snapshot != NULL)
    {
      pthread_mutex_lock(&g_device_lock);
      *snapshot = g_wake_prefs;
      pthread_mutex_unlock(&g_device_lock);
    }
}

int velaride_wake_prefs_set(bool raise, uint8_t sleep)
{
  if (sleep >= VELARIDE_SLEEP_COUNT)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_device_lock);
  g_wake_prefs.raise = raise;
  g_wake_prefs.sleep = sleep;
  g_wake_prefs.save_pending = true;
  g_wake_prefs.save_error = 0;
  g_wake_generation++;
  pthread_mutex_unlock(&g_device_lock);
  return 0;
}

void velaride_ui_prefs_get(struct velaride_ui_prefs_s *snapshot)
{
  if (snapshot != NULL)
    {
      pthread_mutex_lock(&g_device_lock);
      *snapshot = g_ui_prefs;
      pthread_mutex_unlock(&g_device_lock);
    }
}

int velaride_ui_prefs_set(uint8_t accent, uint8_t menu, uint8_t transition)
{
  if (accent >= VELARIDE_ACCENT_COUNT || menu >= VELARIDE_MENU_COUNT ||
      transition >= VELARIDE_TRANSITION_COUNT)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_device_lock);
  g_ui_prefs.accent = accent;
  g_ui_prefs.menu = menu;
  g_ui_prefs.transition = transition;
  g_ui_prefs.save_pending = true;
  g_ui_prefs.save_error = 0;
  g_ui_generation++;
  pthread_mutex_unlock(&g_device_lock);
  return 0;
}

int velaride_display_set(uint8_t manual, bool automatic)
{
  if (manual < 20 || manual > 80)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_device_lock);
  g_display.manual = manual;
  g_display.automatic = automatic;
  g_display.save_pending = true;
  g_display.save_error = 0;
  g_display_generation++;
  g_filter_valid = false;
  g_dark_count = 0;
  pthread_mutex_unlock(&g_device_lock);
  return 0;
}

static uint8_t brightness_for_lux(float lux)
{
  static const float levels[] = {0, 20, 80, 200, 500, 1000};
  static const uint8_t brightness[] = {25, 35, 48, 62, 75, 80};
  unsigned int i;

  for (i = 1; i < sizeof(brightness); i++)
    {
      if (lux < levels[i])
        {
          return brightness[i - 1] + (uint8_t)(
            (lux - levels[i - 1]) / (levels[i] - levels[i - 1]) *
            (brightness[i] - brightness[i - 1]) + 0.5f);
        }
    }

  return 80;
}

uint8_t velaride_display_tick(void)
{
  struct velaride_device_snapshot_s device;
  uint8_t desired;
  uint8_t result;

  velaride_device_get(&device);
  pthread_mutex_lock(&g_device_lock);
  desired = g_display.manual;
  if (g_display.automatic && device.light_valid)
    {
      if (!g_filter_valid || device.light_sample_ms != g_light_filter_ms)
        {
          float alpha = device.light_lux > g_filtered_lux ? 0.65f : 0.35f;
          g_filtered_lux = g_filter_valid ?
            g_filtered_lux + alpha * (device.light_lux - g_filtered_lux) :
            device.light_lux;
          desired = brightness_for_lux(g_filtered_lux);
          if (!g_filter_valid)
            {
              g_auto_desired = g_display.target;
            }
          if (desired >= g_auto_desired + 3)
            {
              g_auto_desired = desired;
              g_dark_count = 0;
            }
          else if (desired + 3 <= g_auto_desired)
            {
              if (++g_dark_count >= 2)
                {
                  g_auto_desired = desired;
                  g_dark_count = 0;
                }
            }
          else
            {
              g_dark_count = 0;
            }
          g_light_filter_ms = device.light_sample_ms;
          g_filter_valid = true;
        }

      desired = g_auto_desired;
      if (desired > g_display.target + 15) desired = g_display.target + 15;
      if (desired + 6 < g_display.target) desired = g_display.target - 6;
    }
  else
    {
      g_filter_valid = false;
      g_dark_count = 0;
    }

  g_display.target = desired;
  result = desired;
  pthread_mutex_unlock(&g_device_lock);
  return result;
}

void velaride_display_applied(uint8_t brightness, int error)
{
  pthread_mutex_lock(&g_device_lock);
  g_display.apply_error = error;
  if (error == 0)
    {
      g_display.applied = brightness;
    }
  pthread_mutex_unlock(&g_device_lock);
}
