/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#ifndef DECODER_H_
#define DECODER_H_

#include <stdint.h>

typedef struct _decoder decoder_t;

decoder_t *initialize_decoder(uintptr_t mram_addr, uint32_t word_id);
void initialize_decoders();

unsigned int decode_int_from(decoder_t *decoder);
void skip_bytes_to_jump(decoder_t *decoder, uint32_t target_address);
unsigned int get_absolute_address_from(decoder_t *decoder);

#endif /* DECODER_H_ */
