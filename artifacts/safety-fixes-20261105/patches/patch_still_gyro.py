"""Remove LSM6DSL gyro zero-rate offset before the ride still/moving decision.

The ride classifier summed |gx|+|gy|+|gz| (mdps, 2000 dps range) against
5000 mdps with no offset removal, so a watch lying still could stay
"moving" forever and the impact -> stillness -> safety check never fired.
The offset is learned during warm-up and tracked only while the
accelerometer is calm. ride-status also reports the raw and compensated
values so the cause can be checked on the device.
Usage: python patch_still_gyro.py <velaride_ride_session.c> <velaride_ride_session.h> <velaride_shell.c>
"""
import sys


def load(path):
    s = open(path, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')


def rep(s, old, new):
    if s.count(old) != 1:
        raise SystemExit('pattern count %d: %s' % (s.count(old), old[:70]))
    return s.replace(old, new)


def save(path, s, nl):
    open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


session_path, header_path, shell_path = sys.argv[1:4]

s, nl = load(session_path)
s = s.replace('\r\n', '\n')
s = rep(s, '#define GYRO_MOVE_SUM 5000\n', '''#define GYRO_MOVE_SUM 5000
#define GYRO_BIAS_SHIFT 4           /* bias kept in 1/16 mdps */
#define GYRO_BIAS_TRACK_SHIFT 4     /* ~4 s time constant at 4 Hz */
#define GYRO_BIAS_LIMIT 8000        /* mdps per axis; larger is motion */
#define GYRO_BIAS_RESCUE 8          /* calm samples before relearning anyway */
''')
s = rep(s, '''  int16_t last_z;
  uint8_t warmup_samples;''', '''  int16_t last_z;
  int32_t gyro_bias[3];
  int32_t dbg_accel_delta;
  int32_t dbg_gyro_raw;
  int32_t dbg_gyro_sum;
  uint8_t calm_samples;
  uint8_t warmup_samples;''')
s = rep(s, '''  if (g_ride.warmup_samples < SENSOR_WARMUP_SAMPLES)
    {
      g_ride.last_x = data.x_data;''', '''  if (g_ride.warmup_samples < SENSOR_WARMUP_SAMPLES)
    {
      ride_gyro_learn(&data, g_ride.warmup_samples == 0 ? 0 : 1);
      g_ride.last_x = data.x_data;''')
s = rep(s, '''  gyro_sum = abs(data.g_x_data) + abs(data.g_y_data) + abs(data.g_z_data);
  impact_sample''', '''  gyro_sum = ride_gyro_deviation(&data);
  g_ride.dbg_accel_delta = accel_delta;
  g_ride.dbg_gyro_raw = abs(data.g_x_data) + abs(data.g_y_data) +
                        abs(data.g_z_data);
  g_ride.dbg_gyro_sum = gyro_sum;

  /* Learn only from samples that already look still, so tumbling with a
   * momentarily steady accelerometer cannot drag the offset; after two calm
   * seconds learn anyway, which recovers from a warm-up taken in motion. */

  if (accel_delta < ACCEL_MOVE_DELTA)
    {
      if (g_ride.calm_samples < UINT8_MAX)
        {
          g_ride.calm_samples++;
        }

      if (gyro_sum < GYRO_MOVE_SUM ||
          g_ride.calm_samples >= GYRO_BIAS_RESCUE)
        {
          ride_gyro_learn(&data, GYRO_BIAS_TRACK_SHIFT);
        }
    }
  else
    {
      g_ride.calm_samples = 0;
    }

  impact_sample''')
s = rep(s, '''void velaride_ride_session_sensor_tick(void)
{''', '''/* shift 0 replaces the offset; otherwise move 1/2^shift toward the sample */

static void ride_gyro_learn(const struct lsm6dsl_sensor_data_s *data,
                            int shift)
{
  const int16_t sample[3] =
  {
    data->g_x_data, data->g_y_data, data->g_z_data
  };

  int i;

  for (i = 0; i < 3; i++)
    {
      int32_t target = sample[i];

      if (target > GYRO_BIAS_LIMIT || target < -GYRO_BIAS_LIMIT)
        {
          continue;
        }

      target *= 1 << GYRO_BIAS_SHIFT;
      g_ride.gyro_bias[i] += (target - g_ride.gyro_bias[i]) / (1 << shift);
    }
}

static int ride_gyro_deviation(const struct lsm6dsl_sensor_data_s *data)
{
  return abs(data->g_x_data - (g_ride.gyro_bias[0] >> GYRO_BIAS_SHIFT)) +
         abs(data->g_y_data - (g_ride.gyro_bias[1] >> GYRO_BIAS_SHIFT)) +
         abs(data->g_z_data - (g_ride.gyro_bias[2] >> GYRO_BIAS_SHIFT));
}

void velaride_ride_session_motion_debug(int32_t *accel_delta,
                                        int32_t *gyro_raw,
                                        int32_t *gyro_sum,
                                        int32_t bias[3])
{
  int i;

  *accel_delta = g_ride.dbg_accel_delta;
  *gyro_raw = g_ride.dbg_gyro_raw;
  *gyro_sum = g_ride.dbg_gyro_sum;
  for (i = 0; i < 3; i++)
    {
      bias[i] = g_ride.gyro_bias[i] >> GYRO_BIAS_SHIFT;
    }
}

void velaride_ride_session_sensor_tick(void)
{''')
save(session_path, s, nl)

h, hnl = load(header_path)
h = h.replace('\r\n', '\n')
h = rep(h, 'void velaride_ride_session_sensor_tick(void);\n', '''void velaride_ride_session_sensor_tick(void);
void velaride_ride_session_motion_debug(int32_t *accel_delta,
                                        int32_t *gyro_raw,
                                        int32_t *gyro_sum,
                                        int32_t bias[3]);
''')
save(header_path, h, hnl)

sh, snl = load(shell_path)
sh = sh.replace('\r\n', '\n')
sh = rep(sh, '''      struct velaride_ride_snapshot_s snapshot;

      velaride_ride_session_get(&snapshot);
      printf("{\\"active\\":%s,\\"paused\\":%s,\\"sensor_ready\\":%s,"
             "\\"motion\\":%d,\\"alert\\":%d,\\"elapsed_s\\":%" PRIu32
             ",\\"moving_s\\":%" PRIu32 ",\\"still_s\\":%" PRIu32
             ",\\"pause_count\\":%u,\\"impact_count\\":%u,"
             "\\"rest_count\\":%u}\\n",''', '''      struct velaride_ride_snapshot_s snapshot;
      int32_t accel_delta;
      int32_t gyro_raw;
      int32_t gyro_sum;
      int32_t bias[3];

      velaride_ride_session_get(&snapshot);
      velaride_ride_session_motion_debug(&accel_delta, &gyro_raw,
                                         &gyro_sum, bias);
      printf("{\\"active\\":%s,\\"paused\\":%s,\\"sensor_ready\\":%s,"
             "\\"motion\\":%d,\\"alert\\":%d,\\"elapsed_s\\":%" PRIu32
             ",\\"moving_s\\":%" PRIu32 ",\\"still_s\\":%" PRIu32
             ",\\"pause_count\\":%u,\\"impact_count\\":%u,"
             "\\"rest_count\\":%u,\\"accel_delta\\":%" PRId32
             ",\\"gyro_raw\\":%" PRId32 ",\\"gyro_sum\\":%" PRId32
             ",\\"gyro_bias\\":[%" PRId32 ",%" PRId32 ",%" PRId32 "]}\\n",''')
sh = rep(sh, '''             snapshot.impact_count, snapshot.rest_count);
      return EXIT_SUCCESS;''', '''             snapshot.impact_count, snapshot.rest_count,
             accel_delta, gyro_raw, gyro_sum, bias[0], bias[1], bias[2]);
      return EXIT_SUCCESS;''')
save(shell_path, sh, snl)
print('patched')
