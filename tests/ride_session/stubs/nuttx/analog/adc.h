#pragma once
#include <stdint.h>
struct __attribute__((packed)) adc_msg_s
{
  uint8_t am_channel;
  int32_t am_data;
};
