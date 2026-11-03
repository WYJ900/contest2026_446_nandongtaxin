#pragma once
#include <stdint.h>
struct sensor_light { uint64_t timestamp; float light; float ir; };
struct sensor_mag { uint64_t timestamp; float x, y, z, temperature; int32_t status; };
