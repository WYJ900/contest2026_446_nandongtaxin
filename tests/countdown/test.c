#include "velaride_countdown.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

static struct velaride_countdown_s get(uint32_t now)
{
  struct velaride_countdown_s result;
  velaride_countdown_get(now, &result);
  return result;
}

int main(void)
{
  struct velaride_countdown_s before;
  assert(velaride_countdown_set(0) == -ERANGE);
  assert(velaride_countdown_set(10801) == -ERANGE);
  assert(velaride_countdown_set(UINT32_MAX) == -ERANGE);
  assert(velaride_countdown_set(10800) == 0);
  assert(get(0).remaining_ms == 10800000);
  assert(velaride_countdown_set(60) == 0);
  velaride_countdown_start(1000);
  assert(get(1500).remaining_ms == 59500);
  velaride_countdown_start(2000); /* Duplicate start must not restart. */
  assert(get(2000).remaining_ms == 59000);
  assert(velaride_countdown_set(5) == -EBUSY);
  velaride_countdown_pause(2500);
  assert(get(500000).state == VELARIDE_COUNTDOWN_PAUSED);
  assert(get(500000).remaining_ms == 58500);
  assert(velaride_countdown_set(5) == -EBUSY);
  velaride_countdown_start(500000);
  assert(get(558499).remaining_ms == 1);
  before = get(558500);
  assert(before.state == VELARIDE_COUNTDOWN_DONE);
  assert(before.completions == 1);
  assert(velaride_countdown_take_done(558500));
  assert(!velaride_countdown_take_done(900000));
  assert(get(900000).completions == 1);
  velaride_countdown_start(900000);
  assert(get(900000).remaining_ms == 60000);
  velaride_countdown_cancel();
  assert(get(1000000).state == VELARIDE_COUNTDOWN_IDLE);
  assert(!velaride_countdown_take_done(1000000));

  /* A 32-bit millisecond wrap cannot add/subtract time. */
  assert(velaride_countdown_set(2) == 0);
  velaride_countdown_start(UINT32_MAX - 999);
  assert(get(0).remaining_ms == 1000);
  assert(get(999).remaining_ms == 1);
  assert(velaride_countdown_take_done(1000));
  assert(!velaride_countdown_take_done(1001));
  assert(get(1001).completions == 2);

  /* Pausing at/after the deadline must finish once, never resurrect time. */
  velaride_countdown_start(2000);
  velaride_countdown_pause(4000);
  assert(get(4000).state == VELARIDE_COUNTDOWN_DONE);
  assert(velaride_countdown_take_done(4000));
  velaride_countdown_start(5000);
  assert(get(7000).state == VELARIDE_COUNTDOWN_DONE);
  velaride_countdown_cancel();
  assert(!velaride_countdown_take_done(7000));
  puts("PASS: bounds, pause/resume, duplicate start, cancel, one-shot completion, wrap and exact deadline");
  return 0;
}
