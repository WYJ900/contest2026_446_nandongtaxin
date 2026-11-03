#pragma once
#include <stdint.h>

#define SNIOC_START 1
#define SNIOC_LSM6DSLSENSORREAD 2

struct lsm6dsl_sensor_data_s
{
  int16_t x_data;
  int16_t y_data;
  int16_t z_data;
  int16_t g_x_data;
  int16_t g_y_data;
  int16_t g_z_data;
};
