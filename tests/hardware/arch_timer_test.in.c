/* The real driver's functions, with the watch's 32-bit arithmetic model. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define FAR
#define weak_function
#define OK 0
#define clock_t uint32_t
#define USEC_PER_TICK ((uint32_t)10000)
#define USEC_PER_SEC 1000000u
#define NSEC_PER_USEC 1000u
#define TICK2USEC(tick) ((tick) * USEC_PER_TICK)
struct timer_status_s { uint32_t timeout, timeleft; };
static struct { void *lower; clock_t timebase; } g_timer;
static struct timer_status_s sample;
static unsigned int reads;
static int mode;
static int getstatus(void *lower, struct timer_status_s *status)
{
  assert(lower);
  reads++;
  *status = sample;
  if (mode == 1 && reads == 1) g_timer.timebase++;
  if (mode == 2)
    {
      g_timer.timebase++;
      sample.timeleft = sample.timeout;
      mode = 0;
    }
  return 0;
}
#define TIMER_GETSTATUS getstatus
/* ACTUAL_DRIVER_FUNCTIONS */

int main(void)
{
  struct timespec ts = {.tv_sec = 123, .tv_nsec = 456};
  uint32_t ticks;
  assert(up_timer_gettime(&ts) == -EAGAIN);
  assert(ts.tv_sec == 123 && ts.tv_nsec == 456);
  g_timer.lower = &g_timer;
  sample.timeout = 10000;
  sample.timeleft = 7500;

  /* At 100 Hz the old multiply falls from 4294960000 to 2704 usec. */
  uint32_t broken_before = TICK2USEC((uint32_t)429496);
  uint32_t broken_after = TICK2USEC((uint32_t)429497);
  assert(broken_after < broken_before);
  uint64_t previous = 0;
  for (uint32_t tick = 429400; tick < 430000; tick++)
    {
      g_timer.timebase = tick;
      uint64_t usec = current_usec();
      assert(usec == (uint64_t)tick * 10000 + 2500);
      assert(usec > previous);
      assert(up_timer_gettick(&ticks) == 0 && ticks == tick);
      assert(up_timer_gettime(&ts) == 0);
      assert((uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000 == usec);
      assert(ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000);
      previous = usec;
    }

  /* Several wraps of the old intermediate, and near the 32-bit tick limit. */
  uint32_t cases[] = {8640000, 60480000, 4000000000u, UINT32_MAX};
  for (unsigned int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
    {
      g_timer.timebase = cases[i];
      assert(current_usec() == (uint64_t)cases[i] * 10000 + 2500);
    }

  g_timer.timebase = 9999;
  reads = 0; mode = 1;
  assert(current_usec() == 100002500 && reads == 2);

  /* A second boundary during the read must produce one consistent sample. */
  g_timer.timebase = 9999;
  sample.timeleft = 1;
  reads = 0; mode = 2;
  assert(up_timer_gettime(&ts) == 0);
  assert(ts.tv_sec == 100 && ts.tv_nsec == 0 && reads == 2);
  g_timer.timebase = 9999;
  sample.timeleft = 1;
  reads = 0;
  assert(up_timer_gettime(&ts) == 0);
  assert(ts.tv_sec == 99 && ts.tv_nsec == 999999000 && reads == 1);
  puts("PASS: actual arch_timer 32-bit overflow reproduction, 71-minute boundary, long uptime, tick conversion, IRQ retry and atomic timespec sample");
}
