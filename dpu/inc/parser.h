/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#ifndef PARSER_H_
#define PARSER_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DOC_INFO, // Description of a new document/positions
    SKIP_INFO, // Starting a new segment, get the next 100th DID and the length of this segment
    END_OF_FRAGMENT,
} parse_did_t;

typedef struct _parser parser_t;

parse_did_t parse_did(parser_t *parser, uint32_t *did, uint32_t *len);
void abort_parse_did(parser_t *parser, uint32_t current_did_len);

void prepare_to_parse_pos_list(parser_t *parser, uint32_t len);
bool parse_pos(parser_t *parser, uint32_t *pos);
void abort_parse_pos(parser_t *parser);

parser_t *setup_parser(uint32_t *words, uint32_t word_id, uint32_t segment_id);

#endif /* PARSER_H_ */
