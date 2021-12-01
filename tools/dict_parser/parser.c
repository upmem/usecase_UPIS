/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

#define _GNU_SOURCE
#include "parser.h"
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#define MAX_LINE_SIZE (1024 * 1024 * 4)

static hash_t word_hash(char *word_name)
{
    size_t size = strlen(word_name);
    hash_t hash = 0;
    for (unsigned int i = 0; i < size; i++) {
        hash += word_name[i];
    }
    return hash;
}

int word_in_dictionnary(char *word_name, struct word_dictionnary *dict)
{
    head_words_t *head = &(dict->dict[word_hash(word_name)]);
    word_t *word;
    SLIST_FOREACH (word, head, next) {
        if (!strcmp(word_name, word->word_name))
            return word->word_id;
    }
    return -1;
}

struct word_dictionnary *parse_dictionnary(char *dictionnary_file_name, FILE *words_map_file)
{
    struct word_dictionnary *dict = calloc(1, sizeof(struct word_dictionnary));
    dict->nb_words = 0;
    dict->dict = calloc(WORDS_HASHTABLE_SIZE, sizeof(head_words_t));

    head_words_t *words_hashtable = dict->dict;
    for (unsigned int i = 0; i < WORDS_HASHTABLE_SIZE; i++) {
        SLIST_INIT(&words_hashtable[i]);
    }

    FILE *dictionnary_file = fopen(dictionnary_file_name, "r");

    char str[MAX_LINE_SIZE];
    while (fgets(str, MAX_LINE_SIZE, dictionnary_file) != NULL) {
        size_t str_len = strlen(str) - 1;
        assert(str_len < MAX_LINE_SIZE - 1);
        str[str_len] = '\0';
        bool skip_word = false;
        for (unsigned each_char = 0; each_char < str_len; each_char++) {
            char curr_char = str[each_char];
            if ((curr_char < 'a' || curr_char > 'z') && (curr_char < 'A' || curr_char > 'Z')) {
                skip_word = true;
                continue;
            } else if (curr_char >= 'A' && curr_char <= 'Z') {
                str[each_char] = 'a' + curr_char - 'A';
            }
        }
        if (skip_word) {
            continue;
        }
        word_t *word = (word_t *)malloc(sizeof(word_t));
        word->word_id = dict->nb_words++;
        word->word_name = strdup(str);
        assert(word->word_name != NULL);
        SLIST_INSERT_HEAD(&words_hashtable[word_hash(word->word_name)], word, next);
        if (words_map_file)
            fprintf(words_map_file, "%u %s\n", word->word_id, word->word_name);
    }
    fclose(dictionnary_file);
    return dict;
}

void free_dictionnary(struct word_dictionnary *dict)
{

    head_words_t *words_hashtable = dict->dict;
    for (int i = 0; i < WORDS_HASHTABLE_SIZE; ++i) {
        while (!SLIST_EMPTY(&words_hashtable[i])) { /* List deletion */
            word_t *n1 = SLIST_FIRST(&words_hashtable[i]);
            SLIST_REMOVE_HEAD(&words_hashtable[i], next);
            free(n1);
        }
    }
    free(dict->dict);
    free(dict);
}
