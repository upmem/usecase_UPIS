/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#ifndef REQUEST_H
#define REQUEST_H

#include <stdint.h>

/* #define STATS_ON */

/**
 * @def MAX_REQUESTED_WORDS
 * @brief the maximum number of words in a request
 *
 */
#define MAX_REQUESTED_WORDS 5

/**
 * @def MAX_ITEM
 * @brief the maximum number of responses sent by a tasklet (ignore any further response)
 *
 */
#define MAX_RESPONSES 1024

/**
 * @typedef algo_request
 * @brief Structure of a request issued by the host.
 * @var nr_words how many words are processed by the request
 * @var args list of words processed by the request
 */
typedef struct algo_request {
    uint32_t nr_words;
    uint32_t args[MAX_REQUESTED_WORDS];
} algo_request_t;

/**
 * The name of the request variable on the DPU
 **/
#define DPU_REQUEST_VAR request

/**
 * Uncomment this define in order to have improved performance.
 * This will skip the statistics collection on the DPU, saving some execution time
 **/
//#define PERF_VERSION

/**
 * @typedef algo_stat
 * @brief structure of statistics
 * @var exec_time total execution time of the algorithm
 * @var nb_results total number of results found by the algorithm
 * @last_tasklet the id of the tasklet that finishes last
 * @var nb_match the number of matches
 * @var nb_bytes_read number of bytes read from MRAM
 * @var nb_bytes_read_useful number of bytes read from MRAM and actually used
 * @var nb_bytes_written number of bytes written to MRAM
 * @var matching_time execution time
 */
typedef struct algo_stats {
#ifndef PERF_VERSION
    uint64_t exec_time;
#endif
    uint32_t nb_results;
#ifndef PERF_VERSION
    uint32_t last_tasklet;
#endif
#ifdef STATS_ON
    uint32_t nb_match;
    uint32_t nb_bytes_read;
    uint32_t nb_bytes_read_useful;
    uint32_t nb_bytes_written;
    uint64_t matching_time;
#endif
} algo_stats_t;
#define DPU_STATS_VAR stat

/**
 * @typedef response
 * @brief structure of a response sent by one tasklet
 * @var did document id
 * @var pos position in document
 * @var req the request id (in case the request is part of a batch)
 */
typedef struct response {
    uint32_t did;
    uint32_t pos : 24;
    uint32_t req : 8;
} response_t;
#define DPU_RESPONSES_VAR responses

/**
 * @def NR_SEGMENTS_PER_MRAM
 * @brief The number of segments used to generate the MRAM files
 */
#define NR_SEGMENTS_PER_MRAM (16)

/**
 * @def NR_REQUESTS_IN_BATCH
 * @brief The maximal number of requests in one batch from the host
 */
#define NR_REQUESTS_IN_BATCH (128)

#endif // REQUEST_H
