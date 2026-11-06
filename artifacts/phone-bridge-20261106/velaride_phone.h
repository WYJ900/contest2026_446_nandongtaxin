/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APPS_EXAMPLES_VELARIDE_PHONE_H
#define __APPS_EXAMPLES_VELARIDE_PHONE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define VELARIDE_PHONE_NOTES 8
struct velaride_phone_note_s {
  char id[33],source[49],title[73],body[161];
  int64_t received_epoch;
};
struct velaride_weather_s {
  char city[49],description[65];
  int temperature_deci;
  int64_t published_epoch;
};
int velaride_phone_note(const char *id,const char *source,const char *title,const char *body);
int velaride_phone_notes_get(struct velaride_phone_note_s *notes,size_t capacity);
void velaride_phone_notes_clear(void);
bool velaride_phone_take_event(void);
int velaride_weather_set(const char *city,const char *description,int temperature_deci,int64_t epoch);
bool velaride_weather_get(struct velaride_weather_s *weather);
void velaride_weather_clear(void);
#endif
