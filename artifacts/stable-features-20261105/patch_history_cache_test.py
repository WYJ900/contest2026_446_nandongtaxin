"""Make the ride-session host test aware of the RAM history cache.

Direct store tampering simulates flash corruption that the watch would only
observe after a reboot, so each tamper and each storage reset drops the cache.
"""
import sys

path = sys.argv[1]
s = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def rep(old, new, count=1):
    global s
    if s.count(old) != count:
        raise SystemExit('pattern count %d: %s' % (s.count(old), old[:70]))
    s = s.replace(old, new)


rep('''  memset(store, 0, sizeof(store));
''', '''  memset(store, 0, sizeof(store));
  g_history_cached = false;
''')
rep('''          store[i].size = size;
          return 0;''', '''          store[i].size = size;
          g_history_cached = false;
          return 0;''')
rep('''          store[i].data[0] ^= 1;
        }
    }
  assert(velaride_ride_session_load_latest(&loaded) == 0); /* Slot recovers bad primary. */''', '''          store[i].data[0] ^= 1;
        }
    }
  g_history_cached = false; /* Corruption is observed after a reboot. */
  assert(velaride_ride_session_load_latest(&loaded) == 0); /* Slot recovers bad primary. */''')
rep('''      store[i].data[0] ^= 1;
  assert(velaride_ride_session_load_latest(&loaded) < 0);''', '''      store[i].data[0] ^= 1;
  g_history_cached = false;
  assert(velaride_ride_session_load_latest(&loaded) < 0);''')
rep('''        store[i].data[0] ^= 1;
    }
  assert(velaride_ride_history_list(sequences, VELARIDE_HISTORY_CAPACITY, &corrupt) == count - 1);''', '''        store[i].data[0] ^= 1;
    }
  g_history_cached = false;
  assert(velaride_ride_history_list(sequences, VELARIDE_HISTORY_CAPACITY, &corrupt) == count - 1);''')

open(path, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('patched', path)
