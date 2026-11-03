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
#define SENSOR_TYPE_MAGNETIC_FIELD 2
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
struct sensor_mag { uint64_t timestamp; float x,y,z,temperature; int32_t status; };
static uint8_t registers[256];
static int bus_error, register_error, allocations, waits, polls;
static struct sensor_lowerhalf_s *registered;
static void *kmm_zalloc(size_t s) { allocations++; return calloc(1,s); }
static void kmm_free(void *p) { allocations--; free(p); }
static int nxsig_usleep(int us) { assert(us == 1000 || us == 20000); waits++; return 0; }
static uint64_t sensor_get_timestamp(void) { return 123456; }
static int sensor_register(struct sensor_lowerhalf_s *p, int n)
{ assert(n == 0 && p->type == SENSOR_TYPE_MAGNETIC_FIELD); if(!register_error)registered=p; return register_error; }
static int transfer(struct i2c_master_s *b, struct i2c_msg_s *m, int count)
{
  assert(b && m[0].addr == 0x30 && m[0].frequency == 100000);
  if(bus_error)return bus_error;
  int reg=m[0].buffer[0];
  if(count==1)
    { assert(m[0].length==2 && m[0].flags==0); registers[reg]=m[0].buffer[1]; }
  else
    {
      assert(count==2 && m[0].length==1 && m[0].flags==I2C_M_NOSTOP && m[1].flags==I2C_M_READ);
      if(reg==0)assert(m[1].length==9);
      if(reg==0x18)polls++;
      memcpy(m[1].buffer,registers+reg,m[1].length);
    }
  return 0;
}
#define I2C_TRANSFER transfer
/* ACTUAL_DRIVER_SOURCE */

static void raw_axis(int axis, uint32_t value)
{
  registers[axis*2]=value>>12;
  registers[axis*2+1]=value>>4;
  registers[6+axis]=(value & 15)<<4;
}
int main(void)
{
  struct i2c_master_s bus={0};
  struct sensor_mag mag;
  assert(mmc5603_register(0,NULL)==-EINVAL);
  assert(mmc5603_register(0,&bus)==-ENODEV && allocations==0);
  registers[0x39]=0x10; register_error=-ENOMEM;
  assert(mmc5603_register(0,&bus)==-ENOMEM && allocations==0);
  register_error=0;
  assert(mmc5603_register(0,&bus)==0 && registers[0x1c]==0x80);
  assert(registered->ops->fetch(registered,NULL,(char*)&mag,sizeof(mag))==-EAGAIN);
  bus_error=-EIO;
  assert(registered->ops->activate(registered,NULL,true)==-EIO);
  bus_error=0;
  assert(registered->ops->activate(registered,NULL,true)==0);
  assert(registers[0x1b]==0x10);
  assert(registered->ops->fetch(registered,NULL,(char*)&mag,1)==-EINVAL);
  polls=0;waits=0;
  assert(registered->ops->fetch(registered,NULL,(char*)&mag,sizeof(mag))==-ETIMEDOUT);
  assert(polls==20 && waits==20);
  registers[0x18]=0x40;
  raw_axis(0,524288+8001);raw_axis(1,524288-3203);raw_axis(2,524288);
  assert(registered->ops->fetch(registered,NULL,(char*)&mag,sizeof(mag))==sizeof(mag));
  assert(fabsf(mag.x-50.00625f)<0.001f && fabsf(mag.y+20.01875f)<0.001f && mag.z==0);
  assert(isnan(mag.temperature) && mag.status==0 && mag.timestamp==123456);
  raw_axis(0,0xfffff);
  assert(registered->ops->fetch(registered,NULL,(char*)&mag,sizeof(mag))==-ERANGE);
  bus_error=-EIO;
  assert(registered->ops->fetch(registered,NULL,(char*)&mag,sizeof(mag))==-EIO);
  bus_error=0;
  assert(registered->ops->activate(registered,NULL,false)==0);
  assert(registered->ops->fetch(registered,NULL,(char*)&mag,sizeof(mag))==-EAGAIN);
  kmm_free(registered);
  puts("PASS: actual MMC5603 lifecycle, 20-bit decoding and scaling, saturation, bus errors and bounded timeout");
}
