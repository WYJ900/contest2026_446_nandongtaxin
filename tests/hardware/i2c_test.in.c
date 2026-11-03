#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define I2C_M_READ 0x0001
#define I2C_M_NOSTOP 0x0040
#define I2C_M_NOSTART 0x0080
#define I2C_MEMADD_SIZE_8BIT 1
#define I2C_MEMADD_SIZE_16BIT 2
#define HAL_OK 0
#define HAL_ERROR 1
#define HAL_BUSY 2
#define HAL_TIMEOUT 3
#define HAL_I2C_STATE_READY 0
#define HAL_I2C_STATE_TIMEOUT 3
#define HAL_I2C_STATE_BUSY_TX 4
#define HAL_I2C_STATE_BUSY_RX 5
#define DEBUGASSERT assert
#define MSEC2TICK(x) (x)
#define I2C_XFER_TIMEOUT 100
#define i2cerr(...) ((void)0)
#define i2cinfo(...) ((void)0)
#define __HAL_I2C_ENABLE(h) ((void)(h))
#define __HAL_I2C_DISABLE(h) ((void)(h))
typedef int HAL_StatusTypeDef;
typedef int HAL_I2C_StateTypeDef;
struct handle_s { int ErrorCode; int state; };
struct i2c_master_s { int unused; };
struct i2c_msg_s
{
  uint32_t frequency;
  uint16_t addr;
  uint16_t flags;
  uint8_t *buffer;
  ssize_t length;
};
struct sifli_i2c_master_s
{
  struct i2c_master_s dev;
  struct handle_s handle;
  int lock;
  int sem_isr;
  bool i2c_dma_flag;
  struct { int dma_rx; int dma_tx; } dma;
};

static int memory_calls;
static int normal_calls;
static int start_result;
static int after_state;
static int wait_result;
static uint16_t last_register;
static struct sifli_i2c_master_s bus;
static int nxmutex_lock(int *lock) { (*lock)++; return 0; }
static int nxmutex_unlock(int *lock) { (*lock)--; return 0; }
static int nxsem_tickwait_uninterruptible(int *sem, int ticks)
{
  (void)sem; (void)ticks;
  bus.handle.state = HAL_I2C_STATE_READY;
  return wait_result;
}
static void HAL_DMA_Init(int *dma) { (void)dma; }
static void HAL_I2C_Reset(struct handle_s *h) { h->state = 0; h->ErrorCode = 0; }
static int HAL_I2C_GetState(struct handle_s *h) { return h->state; }
static void sifli_i2c_configure(struct sifli_i2c_master_s *b, int a,
                              uint32_t freq, uint16_t flags)
{ (void)b; (void)a; (void)freq; (void)flags; }
static int memory_op(struct handle_s *h, uint16_t addr, uint16_t reg,
                     uint16_t size, uint8_t *data, ssize_t len)
{
  (void)size;
  assert(addr == 0x29 && len == 1);
  last_register = reg;
  memory_calls++;
  *data = 0xa0;
  h->state = after_state;
  return start_result;
}
#define HAL_I2C_Mem_Read_IT memory_op
#define HAL_I2C_Mem_Write_IT memory_op
#define HAL_I2C_Mem_Read_DMA memory_op
#define HAL_I2C_Mem_Write_DMA memory_op
static int i2c_normal_ops(struct sifli_i2c_master_s *b, struct i2c_msg_s *msg)
{
  (void)msg;
  normal_calls++;
  b->handle.state = after_state;
  return start_result;
}

/* ACTUAL_DRIVER_FUNCTIONS */

int main(void)
{
  uint8_t reg[] = {0x86, 0x34};
  uint8_t value = 0;
  struct i2c_msg_s messages[] =
  {
    {100000, 0x29, I2C_M_NOSTOP, reg, 1},
    {100000, 0x29, I2C_M_READ, &value, 1}
  };
  assert(sifli_i2c_transfer(&bus.dev, messages, 2) == 0);
  assert(memory_calls == 1 && normal_calls == 0 && last_register == 0x86);
  assert(value == 0xa0 && bus.lock == 0);
  messages[1].flags |= I2C_M_NOSTART;
  assert(sifli_i2c_transfer(&bus.dev, messages, 2) == 0);
  assert(memory_calls == 2);
  reg[0] = 0x12;
  messages[0].length = 2;
  assert(sifli_i2c_transfer(&bus.dev, messages, 2) == 0);
  assert(last_register == 0x1234);
  assert(sifli_i2c_transfer(&bus.dev, messages, 1) == -ENOTSUP);
  assert(sifli_i2c_transfer(&bus.dev, messages + 1, 1) == -EINVAL);
  messages[1].addr = 0x30;
  assert(sifli_i2c_transfer(&bus.dev, messages, 2) == -EINVAL);
  messages[1].addr = 0x29;
  start_result = HAL_ERROR;
  assert(sifli_i2c_transfer(&bus.dev, messages, 2) == -EIO);
  start_result = HAL_TIMEOUT;
  assert(sifli_i2c_transfer(&bus.dev, messages, 2) == -ETIMEDOUT);
  start_result = HAL_OK;
  after_state = HAL_I2C_STATE_BUSY_RX;
  wait_result = -ETIMEDOUT;
  assert(sifli_i2c_transfer(&bus.dev, messages, 2) == -EIO);
  wait_result = 0;
  assert(sifli_i2c_transfer(&bus.dev, messages, 2) == 0);
  assert(bus.lock == 0);
  assert(sifli_i2c_transfer(&bus.dev, NULL, 0) == -EINVAL);
  puts("PASS: I2C standard/legacy reads, byte order, invalid pairs, HAL/IRQ errors");
  return 0;
}
