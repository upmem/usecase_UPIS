/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#ifndef MATCHER_H_
#define MATCHER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _did_matcher did_matcher_t;

did_matcher_t *setup_matchers(uint32_t nr_words, uint32_t *words, uint32_t segment_id);

typedef enum {
    DID_NOT_FOUND = 0,
    DID_FOUND = 1,
    END_OF_INDEX_TABLE = -1,
} seek_did_t;

bool matchers_has_next_did(did_matcher_t *matchers, uint32_t nr_words);
seek_did_t seek_did(did_matcher_t *matchers, uint32_t nr_words, uint32_t pivot);
uint32_t get_max_did(did_matcher_t *matchers, uint32_t nr_words);

typedef enum {
    POSITIONS_NOT_FOUND = 0,
    POSITIONS_FOUND = 1,
    END_OF_POSITIONS = -1,
} seek_pos_t;

bool matchers_has_next_pos(did_matcher_t *matchers, uint32_t nr_words);
seek_pos_t seek_pos(did_matcher_t *matchers, uint32_t nr_words, uint32_t max_pos, uint32_t ref_index);
void get_max_pos_and_index(did_matcher_t *matchers, uint32_t nr_words, uint32_t *index, uint32_t *max_pos);

void start_pos_matching(did_matcher_t *matchers, uint32_t nr_words);
void stop_pos_matching(did_matcher_t *matchers, uint32_t nr_words);

#endif /* MATCHER_H_ */
