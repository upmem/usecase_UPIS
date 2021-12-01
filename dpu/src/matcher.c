/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#include <defs.h>

#include "parser.h"

typedef struct _did_matcher {
    parser_t *parser;
    uint32_t current_did;
    uint32_t current_pos_len;
    uint32_t current_pos;
} did_matcher_t;

#include "matcher.h"
#include "request.h"
#include "trace.h"

static did_matcher_t matchers[NR_SEGMENTS_PER_MRAM][MAX_REQUESTED_WORDS];

// =============================================================================
// INIT MATCHERS FUNCTIONS
// =============================================================================
did_matcher_t *setup_matchers(uint32_t nr_words, uint32_t *words, uint32_t segment_id)
{
    uint32_t each_word;
    did_matcher_t *tasklet_matchers = matchers[me()];
    for (each_word = 0; each_word < nr_words; each_word++) {
        did_matcher_t *matcher = &tasklet_matchers[each_word];
        parser_t *parser = setup_parser(words, each_word, segment_id);
        matcher->parser = parser;
    }
    return tasklet_matchers;
}

// =============================================================================
// DID MATCHING FUNCTIONS
// =============================================================================
static bool matcher_has_next_did(did_matcher_t *matcher)
{
    uint32_t did;
    uint32_t len;
    parse_did_t next_item;
    do {
        next_item = parse_did(matcher->parser, &did, &len);
    } while (next_item == SKIP_INFO);

    if (next_item == END_OF_FRAGMENT)
        return false;

    matcher->current_did = did;
    matcher->current_pos_len = len;

    return true;
}

bool matchers_has_next_did(did_matcher_t *matchers, uint32_t nr_words)
{
    for (uint32_t i = 0; i < nr_words; i++) {
        if (!matcher_has_next_did(&matchers[i]))
            return false;
    }
    return true;
}

seek_did_t seek_did(did_matcher_t *matchers, uint32_t nr_words, uint32_t pivot)
{
    uint32_t nb_matches = 0;
    for (uint32_t i = 0; i < nr_words; i++) {
        // TODO: implement the "segment skip" procedure to accelerate
        // parsing. However, since DID ranges are very small, the
        // improvement would not be that significant with 100 DIDs.
        while (true) {
            uint32_t did = matchers[i].current_did;
            if (did == pivot) {
                nb_matches++;
                break;
            } else if (did > pivot) {
                break;
            }
            abort_parse_did(matchers[i].parser, matchers[i].current_pos_len);
            if (!matcher_has_next_did(&matchers[i]))
                return END_OF_INDEX_TABLE;
        }
    }
    if (nb_matches == nr_words) {
        return DID_FOUND;
    }
    return DID_NOT_FOUND;
}

uint32_t get_max_did(did_matcher_t *matchers, uint32_t nr_words)
{
    uint32_t max = 0;
    for (uint32_t i = 0; i < nr_words; i++) {
        uint32_t this_did = matchers[i].current_did;
        if (this_did > max)
            max = this_did;
    }
    return max;
}

// ============================================================================
// POS MATCHING FUNCTIONS
// ============================================================================
bool matchers_has_next_pos(did_matcher_t *matchers, uint32_t nr_words)
{
    bool parsers_has_next = true;
    for (uint32_t i = 0; i < nr_words; i++) {
        parsers_has_next &= parse_pos(matchers[i].parser, &(matchers[i].current_pos));
    }
    return parsers_has_next;
}

seek_pos_t seek_pos(did_matcher_t *matchers, uint32_t nr_words, uint32_t max_pos, uint32_t ref_index)
{
    uint32_t nb_matches = 0;
    bool no_pos_inc = true;
    for (uint32_t i = 0; i < nr_words; i++) {
        uint32_t pivot = max_pos + i - ref_index;
        while (true) {
            if (matchers[i].current_pos == pivot) {
                nb_matches++;
                break;
            } else if (matchers[i].current_pos > pivot) {
                break;
            }
            no_pos_inc = false;
            if (!parse_pos(matchers[i].parser, &(matchers[i].current_pos)))
                return END_OF_POSITIONS;
        }
    }
    if (nb_matches == nr_words) {
        return POSITIONS_FOUND;
    }
    if (no_pos_inc) {
        // corner case where every word either match or is at a position
        // higher than the pivot. This may happen with doubled words in the search sentence
        // In this case we must look for the next word
        // otherwise we end up in an infinite loop.
        if (!parse_pos(matchers[nr_words - 1].parser, &(matchers[nr_words - 1].current_pos)))
            return END_OF_POSITIONS;
    }
    return POSITIONS_NOT_FOUND;
}

void get_max_pos_and_index(did_matcher_t *matchers, uint32_t nr_words, uint32_t *index, uint32_t *max_pos)
{
    uint32_t _max_pos = 0;
    uint32_t _index;
    for (uint32_t i = 0; i < nr_words; i++) {
        uint32_t pos = matchers[i].current_pos;
        if (pos > _max_pos) {
            _max_pos = pos;
            _index = i;
        }
    }
    *max_pos = _max_pos;
    *index = _index;
}

void start_pos_matching(did_matcher_t *matchers, uint32_t nr_words)
{
    for (uint32_t i = 0; i < nr_words; i++) {
        prepare_to_parse_pos_list(matchers[i].parser, matchers[i].current_pos_len);
    }
}

void stop_pos_matching(did_matcher_t *matchers, uint32_t nr_words)
{
    for (uint32_t i = 0; i < nr_words; i++) {
        abort_parse_pos(matchers[i].parser);
    }
}
