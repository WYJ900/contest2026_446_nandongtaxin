/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_MEMO_H
#define __APPS_EXAMPLES_VELARIDE_MEMO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum velaride_memo_state_e { VELARIDE_MEMO_IDLE, VELARIDE_MEMO_RECORDING,
                            VELARIDE_MEMO_SAVED, VELARIDE_MEMO_ERROR };
struct velaride_memo_s
{
  enum velaride_memo_state_e state;
  uint32_t id;
  uint32_t sequence;
  uint32_t bytes;
  uint32_t crc32;
  bool pending;
  int error;
};
int velaride_memo_start(void);
void velaride_memo_get(struct velaride_memo_s *memo);
int velaride_memo_list(uint32_t *ids, size_t capacity);
int velaride_memo_info(uint32_t id, struct velaride_memo_s *memo);
int velaride_memo_read(uint32_t id, uint32_t offset, void *data, size_t size);
#endif
