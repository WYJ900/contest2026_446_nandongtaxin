#pragma once
#include <stdint.h>
#include <sys/types.h>
#define I2C_M_READ 0x0001
#define I2C_M_NOSTOP 0x0040
#define I2CIOC_TRANSFER 301
struct i2c_msg_s
{
  uint32_t frequency;
  uint16_t addr;
  uint16_t flags;
  uint8_t *buffer;
  ssize_t length;
};
struct i2c_transfer_s
{
  struct i2c_msg_s *msgv;
  size_t msgc;
};
