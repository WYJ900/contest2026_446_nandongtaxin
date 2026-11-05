"""Cache the 16-slot ride history in RAM so UI/BLE reads do not rescan KVDB.

Usage: python patch_history_cache.py <velaride_ride_session.c>
Each scan probes 17 property keys; missing keys are slow (~40 ms each on the
watch), and the scan ran on every watch-face refresh, sport-page entry and
BLE ride_status.  The cache is filled once (init warms it), served from RAM,
and invalidated after every save attempt.
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


rep('''static pthread_mutex_t g_history_lock = PTHREAD_MUTEX_INITIALIZER;
''', '''static pthread_mutex_t g_history_lock = PTHREAD_MUTEX_INITIALIZER;

/* RAM copy of the history slots, newest first.  Guarded by g_history_lock.
 * Probing the 17 KVDB keys costs ~0.6 s with empty slots, so the scan runs
 * once and again only after a save; readers never touch flash. */

static bool g_history_cached;
static int g_history_count;
static unsigned int g_history_bad;
static struct velaride_ride_record_s g_history_records[VELARIDE_HISTORY_CAPACITY];
''')

rep('''static int ride_history_scan(uint32_t *sequences, unsigned int *corrupt,
                             struct velaride_ride_record_s *latest)
{
  struct velaride_ride_record_s record;
  char key[40];
  int count = 0;
  unsigned int bad = 0;
  unsigned int slot;
  int ret;
  int i;
  int j;
''', '''static int ride_history_scan(uint32_t *sequences, unsigned int *corrupt,
                             struct velaride_ride_record_s *records)
{
  struct velaride_ride_record_s record;
  char key[40];
  int count = 0;
  unsigned int bad = 0;
  unsigned int slot;
  int ret;
  int i;
  int j;
''')

rep('''      if (i >= VELARIDE_HISTORY_CAPACITY) continue;
      if (i == 0 && latest != NULL) *latest = record;
      if (count < VELARIDE_HISTORY_CAPACITY) count++;
      for (j = count - 1; j > i; j--) sequences[j] = sequences[j - 1];
      sequences[i] = record.sequence;
    }''', '''      if (i >= VELARIDE_HISTORY_CAPACITY) continue;
      if (count < VELARIDE_HISTORY_CAPACITY) count++;
      for (j = count - 1; j > i; j--)
        {
          sequences[j] = sequences[j - 1];
          records[j] = records[j - 1];
        }
      sequences[i] = record.sequence;
      records[i] = record;
    }''')

rep('''int velaride_ride_history_list(uint32_t *sequences, size_t capacity,
                               unsigned int *corrupt)
{
  int ret;
  if (sequences == NULL || capacity < VELARIDE_HISTORY_CAPACITY) return -EINVAL;
  pthread_mutex_lock(&g_history_lock);
  ret = ride_history_scan(sequences, corrupt, NULL);
  pthread_mutex_unlock(&g_history_lock);
  return ret;
}

int velaride_ride_session_load_latest(struct velaride_ride_record_s *record)
{
  uint32_t sequences[VELARIDE_HISTORY_CAPACITY];
  unsigned int corrupt;
  int count;
  pthread_mutex_lock(&g_history_lock);
  count = ride_history_scan(sequences, &corrupt, record);
  pthread_mutex_unlock(&g_history_lock);
  return count > 0 ? 0 : corrupt > 0 ? -EBADMSG : -ENOENT;
}

int velaride_ride_history_get(uint32_t sequence,
                              struct velaride_ride_record_s *record)
{
  struct velaride_ride_record_s found;
  char key[40];
  int ret;
  if (record == NULL) return -EINVAL;
  pthread_mutex_lock(&g_history_lock);
  ride_history_key(key, sizeof(key), sequence % VELARIDE_HISTORY_CAPACITY);
  ret = ride_load_key(key, &found);
  if (ret < 0 || found.sequence != sequence)
    {
      ret = ride_load_key(RIDE_RECORD_KEY, &found);
    }
  if (ret == 0 && found.sequence == sequence) *record = found;
  else ret = -ENOENT;
  pthread_mutex_unlock(&g_history_lock);
  return ret;
}
''', '''/* Caller holds g_history_lock. */

static void ride_history_ensure(void)
{
  uint32_t sequences[VELARIDE_HISTORY_CAPACITY];

  if (g_history_cached) return;
  g_history_count = ride_history_scan(sequences, &g_history_bad,
                                      g_history_records);
  g_history_cached = true;
}

int velaride_ride_history_list(uint32_t *sequences, size_t capacity,
                               unsigned int *corrupt)
{
  int i;
  int ret;
  if (sequences == NULL || capacity < VELARIDE_HISTORY_CAPACITY) return -EINVAL;
  pthread_mutex_lock(&g_history_lock);
  ride_history_ensure();
  for (i = 0; i < g_history_count; i++)
    sequences[i] = g_history_records[i].sequence;
  if (corrupt != NULL) *corrupt = g_history_bad;
  ret = g_history_count;
  pthread_mutex_unlock(&g_history_lock);
  return ret;
}

int velaride_ride_session_load_latest(struct velaride_ride_record_s *record)
{
  int ret;
  if (record == NULL) return -EINVAL;
  pthread_mutex_lock(&g_history_lock);
  ride_history_ensure();
  if (g_history_count > 0)
    {
      *record = g_history_records[0];
      ret = 0;
    }
  else ret = g_history_bad > 0 ? -EBADMSG : -ENOENT;
  pthread_mutex_unlock(&g_history_lock);
  return ret;
}

int velaride_ride_history_get(uint32_t sequence,
                              struct velaride_ride_record_s *record)
{
  int i;
  int ret = -ENOENT;
  if (record == NULL) return -EINVAL;
  pthread_mutex_lock(&g_history_lock);
  ride_history_ensure();
  for (i = 0; i < g_history_count; i++)
    {
      if (g_history_records[i].sequence == sequence)
        {
          *record = g_history_records[i];
          ret = 0;
          break;
        }
    }
  pthread_mutex_unlock(&g_history_lock);
  return ret;
}
''')

rep('''  ride_history_key(key, sizeof(key), record->sequence % VELARIDE_HISTORY_CAPACITY);
  ret = ride_store_key(key, record);
  if (ret == 0) ret = ride_store_key(RIDE_RECORD_KEY, record);
out:
  pthread_mutex_unlock(&g_history_lock);
  return ret;
}''', '''  ride_history_key(key, sizeof(key), record->sequence % VELARIDE_HISTORY_CAPACITY);
  ret = ride_store_key(key, record);
  if (ret == 0) ret = ride_store_key(RIDE_RECORD_KEY, record);
out:
  /* Flash may hold a partial update after a failure: rescan once. */
  g_history_cached = false;
  pthread_mutex_unlock(&g_history_lock);
  return ret;
}''')

open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('patched', path)
