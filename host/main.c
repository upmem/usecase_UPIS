/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

/**
 * @file main.c
 * @brief executes the algorithm on several DPUs with specific MRAM files
 *
 * This program gets a list of MRAM binary images, containing database fragments, as input.
 * It allocates one DPU per MRAM image, loads each image on its assigned DPU and performs a
 * search with each DPU.
 *
 * Arguments are the MRAM binary images to be used.
 */
#define _GNU_SOURCE
#include <dpu.h>
#include <dpu_description.h>
#include <dpu_management.h>

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "request.h"
#include "parser.h"

#define XSTR(x) #x
#define STR(x) XSTR(x)

DPU_INCBIN(dpu_binary, DPU_BINARY)

#define COLOR_GREEN "\e[00;32m"
#define COLOR_RED "\e[00;31m"
#define COLOR_NONE "\e[0m"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

#define NR_REQUESTS_IN_HOST_BATCH 128

#ifdef STATS_ON
static FILE *stats_file;
#endif

static struct word_dictionnary * dict;
static uint32_t nb_words = 0;

static bool print_response_enabled = false;
static uint32_t nr_requests_in_batch = NR_REQUESTS_IN_HOST_BATCH;

static void print_dpu(const char *msg, long value)
{
    dpu_description_t desc;
    DPU_ASSERT(dpu_get_profile_description(NULL, &desc));
    double dpu_freq = desc->hw.timings.fck_frequency_in_mhz / desc->hw.timings.clock_division;
    dpu_free_description(desc);
    printf("[DPU]  %s = %.3g ms (%.3g Mcc)\n", msg, 1.0e3 * ((double)value) / (dpu_freq * 1.0e6), value / 1e6);
}

#define MRAM_SIZE (62 << 20)
void get_mram_file(const char *path, uint8_t *mram)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        (void)fprintf(stderr, "*** could not open '%s' for reading\n", path);
        exit(EXIT_FAILURE);
    }
    unsigned long byte = fread(mram, 1, MRAM_SIZE, f);
    assert(byte == MRAM_SIZE);
    (void)fclose(f);
}

struct load_and_copy_mram_file_into_dpus_context {
    char *mram_path;
    uint32_t *dpu_offset;
};

dpu_error_t load_and_copy_mram_file_into_dpus(struct dpu_set_t rank, uint32_t rank_id, void *args)
{
    struct load_and_copy_mram_file_into_dpus_context *ctx = (struct load_and_copy_mram_file_into_dpus_context *)args;
    char *mram_path = ctx->mram_path;
    uint32_t *dpu_offset = ctx->dpu_offset;

    void *mram = malloc(MRAM_SIZE);

    struct dpu_set_t dpu;
    unsigned int each_dpu;
    DPU_FOREACH (rank, dpu, each_dpu) {
        char str[512];
        sprintf(str, "%s/%u.bin", mram_path, dpu_offset[rank_id] / nr_requests_in_batch + each_dpu);
        get_mram_file(str, mram);
        DPU_ASSERT(dpu_copy_to(dpu, DPU_MRAM_HEAP_POINTER_NAME, 0, mram, MRAM_SIZE));
    }

    free(mram);

    return DPU_OK;
}

static void print_response_from_dpus(
    struct dpu_set_t dpu_set, response_t *responses, algo_stats_t *stats, uint32_t each_request, uint32_t nbReq)
{

    __attribute__((unused)) struct dpu_set_t dpu;
    unsigned int each_dpu;
    for (uint32_t rq = 0; rq < nbReq; ++rq) {
        unsigned nbResTot = 0;
        bool result_found = false;
        response_t *first_response = NULL;
        DPU_FOREACH (dpu_set, dpu, each_dpu) {
            unsigned nbresp = stats[each_dpu * nr_requests_in_batch + rq].nb_results;
            nbResTot += nbresp;
            if (nbresp > MAX_RESPONSES)
                nbresp = MAX_RESPONSES;

            if (!result_found && nbresp) {
                for (unsigned int each_response = 0; each_response < MAX_RESPONSES * nr_requests_in_batch; each_response++) {
                    response_t *response_tmp = &responses[each_dpu * MAX_RESPONSES * nr_requests_in_batch + each_response];
                    if (response_tmp->req == rq) {
                        first_response = response_tmp;
                        result_found = true;
                        break;
                    }
                }
            }
        }
        if (!result_found)
            printf(">> " COLOR_RED "Request %u:%u : No match found" COLOR_NONE "\n", each_request, rq);
        else
            printf(">> " COLOR_GREEN "Request %u:%u : %u matchs found (first match in document %u at position %u)" COLOR_NONE
                   "\n",
                each_request, rq, nbResTot, first_response->did, first_response->pos);
    }
}

struct get_response_from_dpus_context {
    uint64_t *dpu_slowest;
    double *dpu_average;
    double *rank_average;
    response_t *responses;
    algo_stats_t *stats;
    uint32_t *nr_resp;
    uint32_t *dpu_offset;
    uint32_t nb_batches;
    uint32_t nb_batch;
    uint32_t nb_req;
    long *batch_end_times;
};

dpu_error_t get_response_from_dpus(struct dpu_set_t rank, uint32_t rank_id, void *args)
{
    struct get_response_from_dpus_context *ctx = (struct get_response_from_dpus_context *)args;
    uint32_t *dpu_offset = ctx->dpu_offset;

    unsigned int each_dpu;
    struct dpu_set_t dpu;

    uint32_t max_responses_in_rank = 0;

    DPU_FOREACH (rank, dpu, each_dpu) {
        uint32_t this_dpu = each_dpu * nr_requests_in_batch + dpu_offset[rank_id];

        uint32_t responses_in_dpu = 0;
#ifdef PERF_VERSION
        // for the performance version, we do not provide any statistics on the
        // DPU execution, so we just retrieve the  total number of responses accumulated
        // on the DPU to save computation time on the host
        responses_in_dpu = ctx->nr_resp[this_dpu / nr_requests_in_batch];
#else
        algo_stats_t *stats = ctx->stats;
        uint64_t *slowest = &ctx->dpu_slowest[rank_id * ctx->nb_batches + ctx->nb_batch];
        double *average = &ctx->dpu_average[rank_id * ctx->nb_batches + ctx->nb_batch];
        double *rank_average = &ctx->rank_average[rank_id * ctx->nb_batches + ctx->nb_batch];
        uint64_t slowest_time_dpu = 0;
        for (uint32_t rq = 0; rq < ctx->nb_req; ++rq) {

#ifdef STATS_ON
            fprintf(stats_file,
                "Request %u: RANK:%u DPU:%u nb_matchs:%u bytes_read:%u useful_bytes_read:%u bytes_written:%u match_time:%lu "
                "nb_results:%u\n",
                rq, rank_id, each_dpu, stats[this_dpu + rq].nb_match, stats[this_dpu + rq].nb_bytes_read,
                stats[this_dpu + rq].nb_bytes_read_useful, stats[this_dpu + rq].nb_bytes_written,
                stats[this_dpu + rq].matching_time, stats[this_dpu + rq].nb_results);
#endif
            slowest_time_dpu = MAX(stats[this_dpu + rq].exec_time, slowest_time_dpu);
            responses_in_dpu += stats[this_dpu + rq].nb_results;
        }
        *slowest = MAX(*slowest, slowest_time_dpu);
        *rank_average = MAX(*rank_average, slowest_time_dpu);
        *average += slowest_time_dpu;
#endif
        max_responses_in_rank = MAX(responses_in_dpu, max_responses_in_rank);
    }

    response_t *responses = ctx->responses;
    if (max_responses_in_rank > 0) {
        DPU_FOREACH (rank, dpu, each_dpu) {
            DPU_ASSERT(
                dpu_prepare_xfer(dpu, &responses[(each_dpu * nr_requests_in_batch + dpu_offset[rank_id]) * MAX_RESPONSES]));
        }
        if (max_responses_in_rank > MAX_RESPONSES * nr_requests_in_batch) {
            printf("Warning: number of matches=%d is larger than limit=%d\n", max_responses_in_rank, MAX_RESPONSES);
            max_responses_in_rank = MAX_RESPONSES * nr_requests_in_batch;
        }
        DPU_ASSERT(dpu_push_xfer(
            rank, DPU_XFER_FROM_DPU, STR(DPU_RESPONSES_VAR), 0, sizeof(response_t) * max_responses_in_rank, DPU_XFER_DEFAULT));
    }

    return DPU_OK;
}

#define DEFAULT_MRAM 1
#define DEFAULT_LOOP 1
#define DEFAULT_MRAM_PATH "."

__attribute__((noreturn)) static void usage(FILE *f, int exit_code, const char *exec_name)
{
    /* clang-format off */
    fprintf(f,
            "\nusage: %s [-p <mram_path>] [-m <number_of_mram>] [-l <number_of_loop>] [-n] [-d <dict_path>] [-r <requests_path>] [-t] [-b <batch_size>]\n"
            "\n"
            "\t-p \tthe path to the mram location (default: '" DEFAULT_MRAM_PATH "')\n"
            "\t-m \tthe number of mram to used (default: " STR(DEFAULT_MRAM) ")\n"
            "\t-l \tthe number of loop to run (default: " STR(DEFAULT_LOOP) ")\n"
            "\t-n \tavoid loading the MRAM (to be used with caution)\n"
            "\t-d \tthe path to the dictionary (default: dict.txt)\n"
            "\t-r \tthe path to the file with requests: requests.txt)\n"
            "\t-t \tenable the print of responses\n"
            "\t-b \tthe number of of requests in batch (default: " STR(NR_REQUESTS_IN_HOST_BATCH) ")\n",
            exec_name);
    /* clang-format on */
    exit(exit_code);
}

static void verify_path_exists(const char *path)
{
    if (access(path, R_OK)) {
        fprintf(stderr, "path '%s' does not exist or is not readable (errno: %i)\n", path, errno);
        exit(EXIT_FAILURE);
    }
}

static void parse_args(int argc, char **argv, unsigned int *nb_mram, unsigned int *nb_loop, bool *load_mram, char **mram_path,
    char **dict_path, char **requests_path)
{
    int opt;
    extern char *optarg;
    while ((opt = getopt(argc, argv, "hm:l:np:d:r:tb:")) != -1) {
        switch (opt) {
        case 'p':
            *mram_path = strdup(optarg);
            break;
        case 'm':
            *nb_mram = (unsigned int)atoi(optarg);
            break;
        case 'l':
            *nb_loop = (unsigned int)atoi(optarg);
            break;
        case 'n':
            *load_mram = false;
            break;
        case 'd':
            *dict_path = strdup(optarg);
            break;
        case 'r':
            *requests_path = strdup(optarg);
            break;
        case 't':
            print_response_enabled = true;
            break;
        case 'b':
            nr_requests_in_batch = (unsigned int)atoi(optarg);
            break;
        case 'h':
            usage(stdout, EXIT_SUCCESS, argv[0]);
        default:
            usage(stderr, EXIT_FAILURE, argv[0]);
        }
    }
    verify_path_exists(*mram_path);
}

__attribute__((noinline)) void compute_once(
    struct dpu_set_t dpu_set, struct get_response_from_dpus_context *ctx, algo_request_t *requests)
{
    // broadcast batch of requests to all DPUs
    DPU_ASSERT(dpu_broadcast_to(
        dpu_set, STR(DPU_REQUEST_VAR), 0, (void *)requests, sizeof(algo_request_t) * ctx->nb_req, DPU_XFER_ASYNC));

    // when the number of requests is less than the static size of the batch
    // add a marker at the end so that the DPU knows when to stop
    if (ctx->nb_req < nr_requests_in_batch) {
        static algo_request_t tmp;
        tmp.nr_words = 0;
        DPU_ASSERT(dpu_broadcast_to(
            dpu_set, STR(DPU_REQUEST_VAR), sizeof(algo_request_t) * ctx->nb_req, &tmp, sizeof(algo_request_t), DPU_XFER_ASYNC));
    }

    DPU_ASSERT(dpu_launch(dpu_set, DPU_ASYNCHRONOUS));

    // first transfer the statistics from the DPUs
    struct dpu_set_t dpu, rank;
    uint32_t each_dpu, each_rank;
    DPU_RANK_FOREACH (dpu_set, rank, each_rank) {
        DPU_FOREACH (rank, dpu, each_dpu) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, &ctx->stats[each_dpu * nr_requests_in_batch + ctx->dpu_offset[each_rank]]));
        }
    }
    DPU_ASSERT(dpu_push_xfer(
        dpu_set, DPU_XFER_FROM_DPU, STR(DPU_STATS_VAR), 0, sizeof(algo_stats_t) * nr_requests_in_batch, DPU_XFER_ASYNC));

    // Also transfer the total number of responses stored on each DPU (for all requests)
    // This is the size of the transfer to get the responses
    // This information is also available indirectly in the stats but it is more efficient to
    // transfer it from the DPUs than computing it on the host
    DPU_RANK_FOREACH (dpu_set, rank, each_rank) {
        DPU_FOREACH (rank, dpu, each_dpu) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, &ctx->nr_resp[each_dpu + ctx->dpu_offset[each_rank] / nr_requests_in_batch]));
        }
    }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, "nr_total_responses", 0, sizeof(uint32_t), DPU_XFER_ASYNC));

    DPU_ASSERT(dpu_callback(dpu_set, get_response_from_dpus, ctx, DPU_CALLBACK_ASYNC));
}

__attribute__((noinline)) void compute_loop(
    struct dpu_set_t dpu_set, uint32_t nb_loop, struct get_response_from_dpus_context *ctx, algo_request_t *requests)
{
    for (unsigned int each_loop = 0; each_loop < nb_loop; each_loop++) {
        compute_once(dpu_set, ctx, requests);
    }
}

static void allocated_and_compute(struct dpu_set_t dpu_set, uint32_t nr_ranks, algo_request_t *requests, uint32_t nb_rq,
    uint32_t nb_mram, uint32_t nb_loop, char *mram_path, bool load_mram)
{
    // Set dpu_offset
    uint32_t dpu_offset[nr_ranks];
    dpu_offset[0] = 0;

    struct dpu_set_t rank;
    uint32_t each_rank;
    DPU_RANK_FOREACH (dpu_set, rank, each_rank) {
        uint32_t nr_dpus;
        DPU_ASSERT(dpu_get_nr_dpus(rank, &nr_dpus));
        if (each_rank < nr_ranks - 1) {
            dpu_offset[each_rank + 1] = dpu_offset[each_rank] + nr_dpus * nr_requests_in_batch;
        }
    }

    if (load_mram) {
        printf("Preparing %u MRAMs: loading files\n", nb_mram);
        struct load_and_copy_mram_file_into_dpus_context ctx = { .mram_path = mram_path, .dpu_offset = dpu_offset };
        // Using callback to load each mrams (from disk) in parallel
        DPU_ASSERT(dpu_callback(dpu_set, load_and_copy_mram_file_into_dpus, &ctx, DPU_CALLBACK_DEFAULT));
    } else {
        printf("Using %u MRAMs already loaded\n", nb_mram);
    }

    DPU_ASSERT(
        dpu_broadcast_to(dpu_set, STR(nr_request_in_batch_input), 0, &nr_requests_in_batch, sizeof(uint32_t), DPU_XFER_ASYNC));

    printf("Initializing buffers\n");
    response_t *responses = calloc(nb_mram * MAX_RESPONSES * nr_requests_in_batch, sizeof(response_t));
    algo_stats_t *stats = calloc(nb_mram * nr_requests_in_batch, sizeof(algo_stats_t));
    uint32_t *nr_resp = calloc(nb_mram, sizeof(uint32_t));
    uint32_t nb_batches = (nb_rq + nr_requests_in_batch - 1) / nr_requests_in_batch;
    uint64_t *dpu_slowest = calloc(nr_ranks * nb_batches, sizeof(uint64_t));
    double *dpu_average = calloc(nr_ranks * nb_batches, sizeof(double));
    double *rank_average = calloc(nr_ranks * nb_batches, sizeof(double));
    struct get_response_from_dpus_context *response_ctx = calloc(nb_batches, sizeof(struct get_response_from_dpus_context));
    long *batch_start_times = calloc(nb_batches, sizeof(long));
    long *batch_end_times = calloc(nb_batches, sizeof(long));

    printf("Computing %u loop\n", nb_loop);

    long start, end;
    struct timeval timecheck;
    gettimeofday(&timecheck, NULL);
    start = (long)timecheck.tv_sec * 1e6 + (long)timecheck.tv_usec;
    for (uint32_t i = 0; i < nb_rq; i += nr_requests_in_batch) {

        uint32_t nbReq = nr_requests_in_batch;
        if (i + nbReq > nb_rq)
            nbReq = nb_rq - i;
        assert(nbReq);

        printf("[Host]  Sending batch of requests [%u:%u]\n", i, i + nbReq - 1);

        uint32_t nb_batch = i / nr_requests_in_batch;
        response_ctx[nb_batch].dpu_slowest = dpu_slowest;
        response_ctx[nb_batch].dpu_average = dpu_average;
        response_ctx[nb_batch].rank_average = rank_average;
        response_ctx[nb_batch].responses = responses;
        response_ctx[nb_batch].stats = stats;
        response_ctx[nb_batch].nr_resp = nr_resp;
        response_ctx[nb_batch].dpu_offset = dpu_offset;
        response_ctx[nb_batch].nb_batches = nb_batches;
        response_ctx[nb_batch].nb_batch = nb_batch;
        response_ctx[nb_batch].nb_req = nbReq;
        response_ctx[nb_batch].batch_end_times = batch_end_times;

#ifdef PRINT_STATS
        gettimeofday(&timecheck, NULL);
        batch_start_times[nb_batch] = (long)timecheck.tv_sec * 1e6 + (long)timecheck.tv_usec;
#endif
        compute_loop(dpu_set, nb_loop, &(response_ctx[nb_batch]), requests + i);

        if (print_response_enabled) {
            dpu_sync(dpu_set);
            print_response_from_dpus(dpu_set, responses, stats, i / nr_requests_in_batch, nbReq);
        }

#ifdef PRINT_STATS
        dpu_sync(dpu_set);
        gettimeofday(&timecheck, NULL);
        batch_end_times[nb_batch] = (long)timecheck.tv_sec * 1e6 + (long)timecheck.tv_usec;
        double dpu_average_total = 0.0, rank_average_total = 0.0;
        uint64_t dpu_slowest_total = 0ULL;
        DPU_RANK_FOREACH (dpu_set, rank, each_rank) {
            dpu_average_total += dpu_average[each_rank * nb_batches + i / nr_requests_in_batch];
            rank_average_total += rank_average[each_rank * nb_batches + i / nr_requests_in_batch];
            dpu_slowest_total = MAX(dpu_slowest[each_rank * nb_batches + i / nr_requests_in_batch], dpu_slowest_total);
        }
        print_dpu("slowest execution time      ", dpu_slowest_total);
        print_dpu("average dpu execution time  ", dpu_average_total / (nb_mram * nb_loop));
        print_dpu("average rank execution time ", rank_average_total / (nr_ranks * nb_loop));
#endif
    }
    DPU_ASSERT(dpu_sync(dpu_set));
    gettimeofday(&timecheck, NULL);
    end = (long)timecheck.tv_sec * 1e6 + (long)timecheck.tv_usec;

    double dpu_average_total = 0.0, rank_average_total = 0.0;
    uint64_t dpu_slowest_total = 0ULL;
    DPU_RANK_FOREACH (dpu_set, rank, each_rank) {
        for (uint32_t each_batch = 0; each_batch < nb_batches; ++each_batch) {
            dpu_average_total += dpu_average[each_rank * nb_batches + each_batch];
            rank_average_total += rank_average[each_rank * nb_batches + each_batch];
            dpu_slowest_total = MAX(dpu_slowest[each_rank * nb_batches + each_batch], dpu_slowest_total);
        }
    }
#ifdef PRINT_STATS
    long max_latency = 0;
    for (uint32_t each_batch = 0; each_batch < nb_batches; ++each_batch) {
        printf("[Host]  Latency of batch %u: %.3lfms\n", each_batch,
            (batch_end_times[each_batch] - batch_start_times[each_batch]) / 1e3);
        max_latency = MAX(batch_end_times[each_batch] - batch_start_times[each_batch], max_latency);
    }
    printf("[Host]  Maximum latency of requests: %.3lfms\n", max_latency / 1e3);
#endif

    printf("[Host]  Total time for computing requests: %.3lfs\n", (end - start) / 1e6);

    printf("[Host]  DPU execution times over all requests' batches:\n");
    print_dpu("slowest execution time      ", dpu_slowest_total);
    print_dpu("average dpu execution time  ", dpu_average_total / (nb_mram * nb_loop * nb_batches));
    print_dpu("average rank execution time ", rank_average_total / (nr_ranks * nb_loop * nb_batches));

    free(responses);
    free(stats);
    free(nr_resp);
    free(dpu_slowest);
    free(dpu_average);
    free(rank_average);
    free(batch_start_times);
    free(batch_end_times);
    free(response_ctx);
}

static algo_request_t *parseRequests(FILE *f, uint32_t *nb_rq)
{

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    uint32_t sz = 1;
    algo_request_t *requests = (algo_request_t *)malloc(sz * sizeof(algo_request_t));
    *nb_rq = 0;

    while ((read = getline(&line, &len, f)) != -1) {

        // skip line for file
        if (!strncmp(line, "$$file", 6))
            continue;

        (*nb_rq)++;
        if (*nb_rq > sz) {

            sz *= 2;
            requests = (algo_request_t *)realloc(requests, sz * sizeof(algo_request_t));
        }
        memset(&requests[*nb_rq - 1], 0, sizeof(algo_request_t));
        char *buf = (char *)malloc(len * sizeof(char));
        char *word = line;
        uint32_t i = 0;
        while (sscanf(word, "%s", buf) == 1) {
            assert(i < MAX_REQUESTED_WORDS);

            uint32_t str_len = strlen(buf);
            for (unsigned each_char = 0; each_char < str_len; each_char++) {
                char curr_char = buf[each_char];
                if (curr_char >= 'a' && curr_char <= 'z') {
                    continue;
                } else if (curr_char >= 'A' && curr_char <= 'Z') {
                    buf[each_char] = 'a' + curr_char - 'A';
                } else {
                    buf[each_char] = '\0';
                    break;
                }
            }
            if (buf[0] == '\0')
                continue;
            int wid = word_in_dictionnary(buf, dict);
            if (wid < 0) {
                printf("word %s not in dictionnary !\n", buf);
                assert(0);
            }
            word += strlen(buf) + 1;
            requests[*nb_rq - 1].nr_words++;
            requests[*nb_rq - 1].args[i] = wid;
            ++i;
        }
        free(buf);
    }
    return requests;
}

/**
 * @brief Main of the Host Application.
 *
 * Expects to get a list of MRAM files to load into the target DPUs.
 */
int main(int argc, char **argv)
{
    struct dpu_set_t dpu_set;
    uint32_t nr_ranks;

    unsigned int nb_mram = DEFAULT_MRAM;
    unsigned int nb_loop = DEFAULT_LOOP;
    char *mram_path = DEFAULT_MRAM_PATH;
    char *dict_path = "dict.txt";
    char *requests_path = "requests.txt";
    bool load_mram = true;
    parse_args(argc, argv, &nb_mram, &nb_loop, &load_mram, &mram_path, &dict_path, &requests_path);

    dict = parse_dictionnary(dict_path, NULL);
    nb_words = dict->nb_words;
    uint32_t nb_rq = 0;
    FILE *requests_file = fopen(requests_path, "r");
    if (!requests_file) {
        fprintf(stderr, "Error: please specify valid requests path.\n");
        usage(stderr, EXIT_FAILURE, argv[0]);
    }
    algo_request_t *requests = parseRequests(requests_file, &nb_rq);

#ifdef STATS_ON
    assert((stats_file = fopen("index_search_stats.txt", "w")) != NULL);
#endif

    printf("Allocating DPUs\n");
    DPU_ASSERT(dpu_alloc(nb_mram, "nrJobPerRank=64,dispatchOnAllRanks=true,cycleAccurate=true", &dpu_set));
    printf("DPUs allocated\n");
    DPU_ASSERT(dpu_load_from_incbin(dpu_set, &dpu_binary, NULL));
    DPU_ASSERT(dpu_get_nr_ranks(dpu_set, &nr_ranks));

    allocated_and_compute(dpu_set, nr_ranks, requests, nb_rq, nb_mram, nb_loop, mram_path, load_mram);

    free(requests);

    /*printf("printing log for dpu:\n");*/
    /*struct dpu_set_t dpu;*/
    /*DPU_FOREACH(dpu_set, dpu) {*/
    /*DPU_ASSERT(dpu_log_read(dpu, stdout));*/
    /*}*/

    DPU_ASSERT(dpu_free(dpu_set));

#ifdef STATS_ON
    fclose(stats_file);
#endif

    free_dictionnary(dict);

    return 0;
}
