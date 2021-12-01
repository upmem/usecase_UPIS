/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>

typedef SLIST_HEAD(head_words, word) head_words_t;
typedef struct word {
    char *word_name;
    unsigned word_id;
    SLIST_ENTRY(word) next;
} word_t;

typedef uint8_t hash_t;
#define WORDS_HASHTABLE_SIZE (1 << (CHAR_BIT * sizeof(hash_t)))
struct word_dictionnary {
    unsigned nb_words;
    head_words_t *dict;
};

struct word_dictionnary *parse_dictionnary(char *dictionnary_file_name, FILE *words_map_file);

int word_in_dictionnary(char *word_name, struct word_dictionnary *dict);

void free_dictionnary(struct word_dictionnary *dict);
