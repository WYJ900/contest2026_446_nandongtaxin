#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <kvdb.h>
static uint8_t record[3][12];
static bool present[3];
static int slot(const char *key)
{
  return strcmp(key, "persist.velaride.alarm") == 0 ? 0 :
         strcmp(key, "persist.velaride.night") == 0 ? 1 :
         strcmp(key, "persist.velaride.activity") == 0 ? 2 : -1;
}
ssize_t property_get_buffer(const char *key, void *data, size_t size)
{
  int i = slot(key);
  if (i < 0 || !present[i] || size != sizeof(record[i])) return -ENOENT;
  memcpy(data, record[i], size);
  return size;
}
int property_set_buffer(const char *key, const void *data, size_t size)
{
  int i = slot(key);
  if (i < 0 || size != sizeof(record[i])) return -EINVAL;
  memcpy(record[i], data, size);
  present[i] = true;
  return 0;
}
int property_commit(void) { return 0; }
