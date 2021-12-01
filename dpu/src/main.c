/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

/*
 * Index search on DPUs: main routine.
 *
 * Each tasklet executes the same main function that
 * gets a search request from the system mailbox and performs
 * the requested operation if it has the proper fragments
 * to operate; the meaning of "proper fragments" depending
 * on the request.
 *
 * Arguments to every request are:
 *  - First, the number N of words to the request
 *  - Followed by N word identifiers, N < MAX_REQUESTED_WORDS
 *
 * This file can be compiled with active traces on, by setting the
 * TRACE macro.
 */

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <perfcounter.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "decoder.h"
#include "matcher.h"
#include "request.h"
#include "trace.h"

#ifdef TRACE
STDOUT_BUFFER_INIT(256);
#endif

BARRIER_INIT(barrier, NR_TASKLETS);
MUTEX_INIT(mutex_responses);
MUTEX_INIT(mutex_responses_2);

__mram_noinit response_t DPU_RESPONSES_VAR[MAX_RESPONSES * NR_REQUESTS_IN_BATCH];
__host uint32_t nr_total_responses;

__host uint32_t nr_request_in_batch_input;
__host algo_request_t DPU_REQUEST_VAR[NR_REQUESTS_IN_BATCH];
// NOTE: we could move this structure to the MRAM for faster transfert to the host
// However it is foreseen little gain in performance with this change
__host algo_stats_t DPU_STATS_VAR[NR_REQUESTS_IN_BATCH];

#ifdef STATS_ON
extern uint32_t get_bytes_read(uint32_t, uint32_t);
extern uint32_t get_bytes_read_useful(uint32_t, uint32_t);

#define UPDATE_BYTES_WRITTEN                                                                                                     \
    do {                                                                                                                         \
        mutex_lock(mutex_responses);                                                                                             \
        DPU_STATS_VAR[each_request].nb_bytes_written += sizeof(response);                                                        \
        mutex_unlock(mutex_responses);                                                                                           \
    } while (0)

#define GET_TIME uint64_t start_matching = perfcounter_get();
#define STORE_MATCHING_TIME                                                                                                      \
    do {                                                                                                                         \
        mutex_lock(mutex_responses);                                                                                             \
        DPU_STATS_VAR[each_request].matching_time += perfcounter_get() - start_matching;                                         \
        DPU_STATS_VAR[each_request].nb_match++;                                                                                  \
        mutex_unlock(mutex_responses);                                                                                           \
    } while (0)

#define UPDATE_BYTES_READ                                                                                                        \
    do {                                                                                                                         \
        for (uint32_t each_word = 0; each_word < DPU_REQUEST_VAR[each_request].nr_words; each_word++) {                          \
            DPU_STATS_VAR[each_request].nb_bytes_read_useful += get_bytes_read_useful(each_word, me());                          \
            DPU_STATS_VAR[each_request].nb_bytes_read += get_bytes_read(each_word, me());                                        \
        }                                                                                                                        \
    } while (0)

#else

#define UPDATE_BYTES_WRITTEN
#define GET_TIME
#define STORE_MATCHING_TIME
#define UPDATE_BYTES_READ

#endif

static void can_perform_pos_matching_for_did(uint32_t each_request, did_matcher_t *matchers, unsigned int nr_words, uint32_t did)
{
    start_pos_matching(matchers, nr_words);

    if (!matchers_has_next_pos(matchers, nr_words))
        goto end;

    while (true) {
        uint32_t max_pos, index;

        get_max_pos_and_index(matchers, nr_words, &index, &max_pos);

        switch (seek_pos(matchers, nr_words, max_pos, index)) {
        case POSITIONS_FOUND: {
            uint32_t nresults;
            mutex_lock(mutex_responses);
            nresults = DPU_STATS_VAR[each_request].nb_results++;
            mutex_unlock(mutex_responses);
            if (nresults < MAX_RESPONSES) {
                mutex_lock(mutex_responses_2);
                if (nr_total_responses < MAX_RESPONSES * nr_request_in_batch_input) {
                    uint32_t response_id = nr_total_responses++;
                    mutex_unlock(mutex_responses_2);
                    __dma_aligned response_t response = { .did = did, .pos = max_pos - index, .req = each_request };
                    mram_write(&response, &DPU_RESPONSES_VAR[response_id], sizeof(response));
                    UPDATE_BYTES_WRITTEN;
                } else
                    mutex_unlock(mutex_responses_2);
            }
            goto end;
        }
        case POSITIONS_NOT_FOUND:
            break;
        case END_OF_POSITIONS:
            goto end;
        }
    }
end:
    stop_pos_matching(matchers, nr_words);
}

static void can_perform_did_and_pos_matching(uint32_t each_request, did_matcher_t *matchers, uint32_t nr_words)
{
    while (true) {
        // This is either the initial loop, or we come back from a
        // set of matching DIDs. Whatever the case is, need to
        // warm up the iterator again by fetching next DIDs.
        if (!matchers_has_next_did(matchers, nr_words))
            return;

        seek_did_t did_status;
        do {
            uint32_t did = get_max_did(matchers, nr_words);
            did_status = seek_did(matchers, nr_words, did);
            switch (did_status) {
            case END_OF_INDEX_TABLE:
                return;
            case DID_FOUND: {
                PRINT("|%u FOUND MATCHING DID::: %d - Checking positions...\n", me(), did);
                GET_TIME;
                can_perform_pos_matching_for_did(each_request, matchers, nr_words, did);
                STORE_MATCHING_TIME;
            } break;
            case DID_NOT_FOUND:
                break;
            }
        } while (did_status == DID_NOT_FOUND);
    }
}

#define TASKLET_LOAD_SHARE

#ifdef TASKLET_LOAD_SHARE

MUTEX_INIT(job_mutex);
int jobid;
#endif

int main()
{
    if (me() == 0) {
#ifndef PERF_VERSION
        perfcounter_config(COUNT_CYCLES, true);
#endif
        mem_reset();
        memset(&DPU_STATS_VAR, 0, sizeof(DPU_STATS_VAR));
        nr_total_responses = 0;
        initialize_decoders();
#ifdef TASKLET_LOAD_SHARE
        jobid = 0;
#endif
        assert(NR_REQUESTS_IN_BATCH >= nr_request_in_batch_input);
    }

    barrier_wait(&barrier);

#ifdef TASKLET_LOAD_SHARE
    uint32_t id;
    _Static_assert(__builtin_popcount(NR_SEGMENTS_PER_MRAM) == 1, "NR_SEGMENTS_PER_MRAM should be a power of 2.");
    int nr_segments_exp = __builtin_log2(NR_SEGMENTS_PER_MRAM);
    while (1) {

        mutex_lock(job_mutex);
        id = jobid++;
        mutex_unlock(job_mutex);

        if (id >= nr_request_in_batch_input << nr_segments_exp)
            break;

        uint32_t each_request = id >> nr_segments_exp;
        uint32_t each_segment = id - (each_request << nr_segments_exp);
        if (DPU_REQUEST_VAR[each_request].nr_words == 0)
            break;

        did_matcher_t *matchers
            = setup_matchers(DPU_REQUEST_VAR[each_request].nr_words, DPU_REQUEST_VAR[each_request].args, each_segment);
        can_perform_did_and_pos_matching(each_request, matchers, DPU_REQUEST_VAR[each_request].nr_words);
        UPDATE_BYTES_READ;
#ifndef PERF_VERSION
        DPU_STATS_VAR[each_request].exec_time = perfcounter_get();
        DPU_STATS_VAR[each_request].last_tasklet = me();
#endif
    }
#else
    for (uint32_t each_request = 0; each_request < nr_request_in_batch_input; ++each_request) {
        // the number of requests may be less than the request array capacity
        if (DPU_REQUEST_VAR[each_request].nr_words == 0)
            break;
        for (uint32_t each_segment = me(); each_segment < NR_SEGMENTS_PER_MRAM; each_segment += NR_TASKLETS) {
            did_matcher_t *matchers
                = setup_matchers(DPU_REQUEST_VAR[each_request].nr_words, DPU_REQUEST_VAR[each_request].args, each_segment);
            can_perform_did_and_pos_matching(each_request, matchers, DPU_REQUEST_VAR[each_request].nr_words);
            UPDATE_BYTES_READ;
        }

#ifndef PERF_VERSION
        DPU_STATS_VAR[each_request].exec_time = perfcounter_get();
        DPU_STATS_VAR[each_request].last_tasklet = me();
#endif
    }
#endif

    return 0;
}
