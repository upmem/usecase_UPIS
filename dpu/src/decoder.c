/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

/*
 * Implementation of word search algorithm on DPUs.
 *
 * Depending on whether the macro TRACE is defined or not the
 * program will either print out the test results with printf
 * (to verify that the result is correct) or simply run the
 * specified test (to get accurate performance measures).
 *
 * The input to every test is a pre-defined MRAM containing the
 * DID/position tables of five words within a given range of DIDs.
 *
 * Data structure
 * ==============
 * The database resides in MRAM and is organized as follows:
 *
 *  00000000   WORD 0 DB ADDRESS [= 0x1C]
 *  00000004   WORD 1 DB ADDRESS
 *  00000008   WORD 2 DB ADDRESS
 *  0000000C   WORD 3 DB ADDRESS
 *  00000010   WORD 4 DB ADDRESS
 *  00000014   END ADDRESS
 *  0000001C   WORD 0 DB
 *    ...
 *             WORD 1 DB
 *    ...        ...
 *
 * Every database is stored on a 64-bits aligned address
 * and starts with two words:
 *  - FIRST DID: first document id. for this section of data
 *  - LAST DID: last document id. for this section of data
 *
 * Notice that the implementation does not skip segments, so
 * that the measured times correspond to worst case situations.
 * Please refer to seek_did for this point.
 */

#include <attributes.h>
#include <defs.h>
#include <mram.h>
#include <seqread.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "request.h"
#include "trace.h"

// counting the number of bytes read will make the measure of time to match not accurate
/* #define COUNT_BYTES_READ */

typedef struct _decoder {
    seqreader_t reader;
    uint8_t *ptr;
#if defined(STATS_ON) & defined(COUNT_BYTES_READ)
    uint32_t nb_bytes_read;
    uint32_t nb_bytes_read_useful;
#endif
} decoder_t;

static decoder_t decoders[NR_SEGMENTS_PER_MRAM][MAX_REQUESTED_WORDS];

#if defined(STATS_ON) & defined(COUNT_BYTES_READ)
#define READ_BYTE(decoder) decoder->nb_bytes_read_useful++;
#define READ_256_BYTES(decoder) decoder->nb_bytes_read += 256;
uint32_t get_bytes_read(uint32_t word_id, uint32_t segment_id) { return decoders[segment_id][word_id].nb_bytes_read; }
uint32_t get_bytes_read_useful(uint32_t word_id, uint32_t segment_id)
{
    return decoders[segment_id][word_id].nb_bytes_read_useful;
}
#else
#define READ_BYTE(decoder)
#define READ_256_BYTES(decoder)
uint32_t get_bytes_read(__attribute__((unused)) uint32_t word_id, __attribute__((unused)) uint32_t segment_id) { return 0; }
uint32_t get_bytes_read_useful(__attribute__((unused)) uint32_t word_id, __attribute__((unused)) uint32_t segment_id)
{
    return 0;
}
#endif

// ============================================================================
// LOAD AND DECODE DATA
// ============================================================================
void skip_bytes_to_jump(decoder_t *decoder, uint32_t target_address)
{
    uintptr_t prev_mram = decoder->reader.mram_addr;
    decoder->ptr = seqread_seek((__mram_ptr void *)target_address, &(decoder->reader));
    if (prev_mram != decoder->reader.mram_addr) {
        READ_256_BYTES(decoder);
    }
}

void initialize_decoders()
{

    for (int i = 0; i < NR_SEGMENTS_PER_MRAM; ++i) {
        for (int j = 0; j < MAX_REQUESTED_WORDS; ++j) {
            decoders[i][j].ptr
                = seqread_init(seqread_alloc(), (__mram_ptr void *)(uintptr_t)DPU_MRAM_HEAP_POINTER, &(decoders[i][j].reader));
        }
    }
}

decoder_t *initialize_decoder(uintptr_t mram_addr, uint32_t word_id)
{
    decoder_t *decoder = &decoders[me()][word_id];
    decoder->ptr = seqread_seek((__mram_ptr void *)(mram_addr + (uintptr_t)DPU_MRAM_HEAP_POINTER), &(decoder->reader));
    return decoder;
}

// Computes an absolute address from the current position of this decoder in
// memory.
unsigned int get_absolute_address_from(decoder_t *decoder)
{
    return (unsigned int)seqread_tell(decoder->ptr, &(decoder->reader));
}

// Fetches an integer value from a parsed buffer.
// Returns the decoded value.
uint32_t decode_int_from(decoder_t *decoder)
{
    uint32_t value = 0;
    uint8_t byte;
    uint32_t byte_shift = 0;
    uint8_t *ptr = decoder->ptr;
    uintptr_t prev_mram = decoder->reader.mram_addr;

    do {
        byte = *ptr;
        value = value | ((byte & 127) << byte_shift);
        byte_shift += 7;

        ptr = seqread_get(ptr, sizeof(uint8_t), &(decoder->reader));
        READ_BYTE(decoder);
    } while (!(byte & 128));

    if (prev_mram != decoder->reader.mram_addr) {
        READ_256_BYTES(decoder);
    }
    decoder->ptr = ptr;
    return value;
}
