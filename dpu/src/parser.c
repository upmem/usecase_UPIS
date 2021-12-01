/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#include <defs.h>
#include <mram.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "decoder.h"

typedef struct _parser {
    decoder_t *decoder;
    struct {
        int32_t nr_bytes_left; // How many bytes still to be read
        uint32_t current_pos; // Last position recorded during the parsing
        uint32_t next_item_address; // Used to skip to the next document when aborting position iteration.
    } pos_parser;
    struct {
        uint32_t nr_item_read; // Number of DID/POS read up to now... Will be reset every NR_DID_PER_SEGMENT items.
        uint32_t current_did; // Either the first DID of a new segment, or the last DID read in the current segment.
        uint32_t last_did;
    } did_parser;
} parser_t;

#include "parser.h"
#include "request.h"
#include "trace.h"

#define NR_DID_PER_SEGMENT 100

static parser_t parsers[NR_SEGMENTS_PER_MRAM][MAX_REQUESTED_WORDS];

// ============================================================================
// INIT PARSERS FUNCTIONS
// ============================================================================
static parser_t *initialize_parser(uintptr_t mram_addr, uint32_t word_id)
{
    parser_t *parser = &parsers[me()][word_id];

    parser->decoder = initialize_decoder(mram_addr, word_id);
    parser->did_parser.nr_item_read = NR_DID_PER_SEGMENT;

    parser->did_parser.current_did = 0;
    parser->did_parser.last_did = decode_int_from(parser->decoder);

    return parser;
}

parser_t *setup_parser(uint32_t *words, uint32_t word_id, uint32_t segment_id)
{
    __mram_ptr uintptr_t *mram_header = (__mram_ptr uintptr_t *)DPU_MRAM_HEAP_POINTER;
    uint32_t mram_addr_offset = words[word_id] * NR_SEGMENTS_PER_MRAM + segment_id;
    uintptr_t mram_addr = mram_header[mram_addr_offset];

    return initialize_parser(mram_addr, word_id);
}

// ============================================================================
// PARSER DID FUNCTIONS
// ============================================================================

parse_did_t parse_did(parser_t *parser, uint32_t *did, unsigned int *len)
{
    uint32_t delta_did = decode_int_from(parser->decoder);
    *len = decode_int_from(parser->decoder);
    if (parser->did_parser.nr_item_read == NR_DID_PER_SEGMENT) {
        parser->did_parser.nr_item_read = 0;
        return SKIP_INFO;
    } else {
        uint32_t next_did = parser->did_parser.current_did + delta_did;
        if (next_did == parser->did_parser.last_did)
            return END_OF_FRAGMENT;
        *did = parser->did_parser.current_did = next_did;
        parser->did_parser.nr_item_read += 1;
        return DOC_INFO;
    }
}

void abort_parse_did(parser_t *parser, uint32_t current_did_len)
{
    skip_bytes_to_jump(parser->decoder, get_absolute_address_from(parser->decoder) + current_did_len);
}

// ============================================================================
// PARSER POS FUNCTIONS
// ============================================================================

// Sets up the parser to decode a series of positions. The parameter specifies
// the size of the position list, in bytes
void prepare_to_parse_pos_list(parser_t *parser, uint32_t len)
{
    parser->pos_parser.nr_bytes_left = (int32_t)len;
    parser->pos_parser.current_pos = 0;
    parser->pos_parser.next_item_address = get_absolute_address_from(parser->decoder) + len;
}

// Gets the next position. Returns true if more positions are expected.
bool parse_pos(parser_t *parser, uint32_t *pos)
{
    uint32_t this_pos;
    unsigned int this_pos_addr;

    if (parser->pos_parser.nr_bytes_left <= 0)
        return false;

    this_pos_addr = get_absolute_address_from(parser->decoder);
    this_pos = decode_int_from(parser->decoder);

    parser->pos_parser.nr_bytes_left -= (get_absolute_address_from(parser->decoder) - this_pos_addr);
    parser->pos_parser.current_pos += this_pos;

    *pos = parser->pos_parser.current_pos;
    return true;
}

// Aborts the parsing of positions, moving the cursor to the next
// document or segment descriptor.
void abort_parse_pos(parser_t *parser) { skip_bytes_to_jump(parser->decoder, parser->pos_parser.next_item_address); }
