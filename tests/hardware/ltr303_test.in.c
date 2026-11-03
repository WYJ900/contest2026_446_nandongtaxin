#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define FAR
#define I2C_M_NOSTOP 64
#define I2C_M_READ 1
#define SENSOR_TYPE_LIGHT 5
struct file { int unused; };
struct i2c_master_s { int unused; };
struct i2c_msg_s { int frequency, addr, flags; uint8_t *buffer; int length; };
struct sensor_lowerhalf_s;
struct sensor_ops_s
{
  int (*activate)(struct sensor_lowerhalf_s *, struct file *, bool);
  int (*fetch)(struct sensor_lowerhalf_s *, struct file *, char *, size_t);
};
struct sensor_lowerhalf_s { int type; const struct sensor_ops_s *ops; };
struct sensor_light { uint64_t timestamp; float light, ir; };
static uint8_t registers[256];
static int bus_error;
static int register_error;
static int allocations;
static int drained;
static struct sensor_lowerhalf_s *registered;
static void *kmm_zalloc(size_t size) { allocations++; return calloc(1, size); }
static void kmm_free(void *ptr) { allocations--; free(ptr); }
static int nxsig_usleep(int us) { assert(us == 100000); return 0; }
static uint64_t sensor_get_timestamp(void) { return 123456; }
static int sensor_register(struct sensor_lowerhalf_s *p, int n)
{
  assert(n == 0 && p->type == SENSOR_TYPE_LIGHT);
  if (!register_error) registered = p;
  return register_error;
}
static int transfer(struct i2c_master_s *bus, struct i2c_msg_s *msg, int count)
{
  assert(bus && msg[0].addr == 0x29 && msg[0].frequency == 100000);
  if (bus_error) return bus_error;
  uint8_t reg = msg[0].buffer[0];
  if (count == 1)
    {
      assert(msg[0].length == 2 && msg[0].flags == 0);
      registers[reg] = msg[0].buffer[1];
    }
  else
    {
      assert(count == 2 && msg[0].length == 1);
      assert(msg[0].flags == I2C_M_NOSTOP && msg[1].flags == I2C_M_READ);
      memcpy(msg[1].buffer, registers + reg, msg[1].length);
      if (reg == 0x88) { assert(msg[1].length == 4); drained++; }
    }
  return 0;
}
#define I2C_TRANSFER transfer

/* ACTUAL_DRIVER_SOURCE */

int main(void)
{
  struct i2c_master_s bus = {0};
  struct sensor_light sample;
  assert(ltr303_register(0, NULL) == -EINVAL);
  assert(ltr303_register(0, &bus) == -ENODEV && allocations == 0);
  registers[0x86] = 0xa0; registers[0x87] = 5;
  bus_error = -ETIMEDOUT;
  assert(ltr303_register(0, &bus) == -ETIMEDOUT && allocations == 0);
  bus_error = 0; register_error = -ENOMEM;
  assert(ltr303_register(0, &bus) == -ENOMEM && allocations == 0);
  register_error = 0;
  assert(ltr303_register(0, &bus) == 0 && allocations == 1);
  assert(registered->ops->fetch(registered, NULL, (char *)&sample, sizeof(sample)) == -EAGAIN);
  assert(registered->ops->activate(registered, NULL, true) == 0);
  assert(registers[0x80] == 1 && registers[0x85] == 2);
  assert(registered->ops->fetch(registered, NULL, (char *)&sample, 1) == -EINVAL);
  assert(registered->ops->fetch(registered, NULL, (char *)&sample, sizeof(sample)) == -EAGAIN);
  registers[0x8c] = 4;
  registers[0x88] = 20; registers[0x8a] = 100;
  assert(registered->ops->fetch(registered, NULL, (char *)&sample, sizeof(sample)) == sizeof(sample));
  assert(fabsf(sample.light - 199.548f) < 0.01f && isnan(sample.ir));
  assert(sample.timestamp == 123456 && drained == 1);
  assert(ltr303_lux(0, 0) == 0);
  assert(fabsf(ltr303_lux(100, 100) - 232.37f) < 0.01f);
  assert(fabsf(ltr303_lux(100, 200) - 82.96f) < 0.01f);
  assert(ltr303_lux(10, 1000) == 0);
  registers[0x8c] = 0x84;
  assert(registered->ops->fetch(registered, NULL, (char *)&sample, sizeof(sample)) == -ERANGE);
  assert(drained == 2);
  registers[0x8c] = 0x14;
  assert(registered->ops->fetch(registered, NULL, (char *)&sample, sizeof(sample)) == -ERANGE);
  registers[0x8c] = 4;
  registers[0x8a] = registers[0x8b] = 255;
  assert(registered->ops->fetch(registered, NULL, (char *)&sample, sizeof(sample)) == -ERANGE);
  bus_error = -EIO;
  assert(registered->ops->fetch(registered, NULL, (char *)&sample, sizeof(sample)) == -EIO);
  assert(registered->ops->activate(registered, NULL, false) == -EIO);
  assert(((struct ltr303_dev_s *)registered)->enabled);
  bus_error = 0;
  assert(registered->ops->activate(registered, NULL, false) == 0);
  assert(registers[0x80] == 0);
  kmm_free(registered);
  puts("PASS: actual LTR303 identity, lifecycle, configuration, coherent burst, lux branches, saturation and I2C errors");
}
