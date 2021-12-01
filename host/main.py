#!/usr/bin/env python3

import dpu

import argparse
import re
import time
import subprocess
import sys
import struct
import os
from itertools import zip_longest
import binascii


parser = argparse.ArgumentParser(
    description='')
parser.add_argument(
    "--dpu-program", "-d",
    required=True,
    metavar="<dpu_program>",
    help="The path to the DPU program to use")
parser.add_argument(
    "--mrams-path", "-m",
    required=True,
    metavar="<path_to_mrams>",
    help="The path to the MRAMs to use")
parser.add_argument(
    "--nr-mrams", "-n",
    required=True,
    metavar="<nr_mrams>",
    type=int,
    help="Number of mrams in the path to mrams")
parser.add_argument(
    "--input", "-i",
    default=None,
    metavar="<inputs_file>",
    help="File containing this inputs to compute, if not specified run in interactive mode")
parser.add_argument(
    "--skip-load", "-s",
    default=False,
    metavar="<skip_load>",
    type=int,
    help="If true skip loading the mrams. Need to be use with caution.")
args = parser.parse_args()

int_size = 4 # in bytes
byte_size = 8
stats_size = int_size * 4
max_responses = 1024
response_size = int_size * 2
max_input = 5
request_size = (max_input + 1) * int_size
mram_size = 62 << 20
NR_REQUESTS_IN_BATCH = 128

word_map_filename = args.mrams_path + "/words.txt"
word_map = {}
print("Reading '%s'" % (word_map_filename))
for line in open(word_map_filename).readlines():
    word_id, word = line.split(" ")
    word_map[word.rstrip()] = int(word_id)

doc_map_filename = args.mrams_path + "/docs.txt"
doc_map = {}
print("Reading '%s'" % (doc_map_filename))
for line in open(doc_map_filename).readlines():
    doc_id, doc_path = line.split(" ")
    doc_map[doc_id] = doc_path


def input_buffer_from_words(words):
    words_id = [word_map[word] for word in words]

    input_buffer = bytearray(request_size)
    struct.pack_into("<I", input_buffer, 0, len(words))
    for idx, word_id in enumerate(words_id):
        struct.pack_into("<I", input_buffer, int_size * (idx + 1), word_id)
    return input_buffer


def set_input(sentence):
    words = list(
        filter(lambda word: len(word) > 0,
               [word.lower() for word in re.split("[^a-zA-Z]", sentence)]))
    if len(words) > max_input:
        print("Too many words entered, "
              + str(max_input)
              + " words maximum")
        return None, None, True
    for word in words:
        if word not in word_map:
            print("'" + word + "' is not in dictionary")
            return None, None, True
    return words, input_buffer_from_words(words), False


def output_responses_batch(dpu_set, unused, args):
    outputs, stats, request_words, rank_partitioning, out = args

    total_time = 0

    for nr_batch in range(len(outputs)):
        
        batch_time = 0

        for nr_request in range(NR_REQUESTS_IN_BATCH):
            
            # if this is an empty request (last batch may be incomplete)
            if(len(request_words[nr_batch][nr_request]) == 0):
                continue

            tot_responses = 0
            slowest_time = 0
            slowest_rank = 0
            slowest_dpu = 0
            slowest_nr_responses = 0
            slowest_last_tasklet = 0

            for rank_id, rank in enumerate(dpu_set.ranks()):
                for dpu_id, dpu in enumerate(rank):
                    
                    output = outputs[nr_batch][rank_id][dpu_id]
                    nr_responses = struct.unpack_from("<I", 
                            stats[nr_batch][rank_partitioning[rank_id] + dpu_id], 
                            nr_request * stats_size + 2 * int_size)[0]
                    tot_responses = tot_responses + nr_responses

                    this_time = struct.unpack_from("<q", 
                        stats[nr_batch][rank_partitioning[rank_id] + dpu_id], 
                        nr_request * stats_size)[0]

                    last_tasklet = struct.unpack_from("<I", 
                        stats[nr_batch][rank_partitioning[rank_id] + dpu_id], 
                        nr_request * stats_size + 3 * int_size)[0]

                    if this_time > slowest_time:
                        slowest_time = this_time
                        slowest_rank = rank_id
                        slowest_dpu = dpu_id
                        slowest_nr_responses = nr_responses
                        slowest_last_tasklet = last_tasklet

            if slowest_time > batch_time:
                batch_time = slowest_time
            print("Request %d:%d: Num. matches: %u Num. cycles: %ld. Slowest %d:%d tasklet:%d resp:%d" % (nr_batch, nr_request, tot_responses, 
                slowest_time, slowest_rank, slowest_dpu, slowest_last_tasklet, slowest_nr_responses), flush=True)

        total_time = total_time + batch_time

    print("Total time for batch: %d" % (total_time))

def set_input_from_stdin():
    print("")
    again = False
    words=[]
    while again or len(words) < 1 or len(words) > max_input:
        sentence = input("Enter words (" + str(max_input) + " max): ")
        words, inputs, again = set_input(sentence)
    return words, inputs

def output_responses(dpu_set, unused, args):
    outputs, stats, rank_partitioning, request_words, out = args
    responses = []
    for rank_id, rank in enumerate(dpu_set.ranks()):
        for dpu_id, dpu in enumerate(rank):
            output = outputs[rank_id][dpu_id]
            nr_responses = struct.unpack_from("<I", stats[rank_partitioning[rank_id] + dpu_id], 2 * int_size)[0]
            if nr_responses > max_responses:
                print("Too many responses in dpu (" + str(dpu) + ")", file = out)
                nr_responses = max_responses
            for response in range(nr_responses):
                responses.append(struct.unpack_from("<II", output, 2 * int_size * response))
    for response_id, (doc_id, pos_id) in enumerate(responses):
        if response_id >= 5:
            break
        doc = doc_map[str(doc_id)].rstrip()
        print("\tFound in " + os.path.basename(doc), file = out)
        print("##############################################", file = out)
        command = [
            "grep",
            '--color=always',
            '-iE',
            '-C', '1',
            r"\<" + r"\>[^a-zA-Z]*\<".join(request_words) + r"\>",
            doc]
        subprocess.call(command, stdout=out, stderr=out)
        print("##############################################", file = out)
    print("\t%u results found" % (len(responses)), file = out)


def compute_get_results(rank, rank_id, args):
    outputs, stats, rank_partitioning, n_rq = args
    first_dpu_id = rank_partitioning[rank_id]
    max_results = max(sum(struct.unpack_from("<I", stats[first_dpu_id + dpu_id], nr_bat * stats_size + 2 * int_size)[0] 
        for nr_bat in range(NR_REQUESTS_IN_BATCH)) for dpu_id in range(len(rank)))
    if (max_results > max_responses * NR_REQUESTS_IN_BATCH):
        print("Warning: number of responses %d exceeds max %d" % (max_results, max_responses * NR_REQUESTS_IN_BATCH))
        max_results = max_responses
    rank_outputs = outputs[rank_id]
    rank.copy(rank_outputs, "responses", size = max_results * response_size, async_mode = False)


def compute(dpu_set, outputs, stats, input_buffer, rank_partitioning, n_rq):
    dpu_set.copy("request", input_buffer)
    dpu_set.exec()
    dpu_set.copy(stats, "stat")
    dpu_set.call(compute_get_results, [outputs, stats, rank_partitioning, n_rq])


def load_mrams(rank, rank_id, args):
    rank_partitioning, mrams_path = args
    mrams = []
    first_dpu_id = rank_partitioning[rank_id]
    for dpu_id, dpu in enumerate(rank):
        with open(mrams_path + "/" + str(first_dpu_id + dpu_id) + ".bin", "rb") as mram:
            mrams.append(bytearray(mram.read(mram_size)))
    rank.copy("__sys_used_mram_end", mrams, async_mode = False)


def create_rank_partitioning(dpu_set):
    nb_dpus = 0
    rank_partitioning = {}
    for rank_id, rank in enumerate(dpu_set.ranks()):
        rank_partitioning[rank_id] = nb_dpus
        nb_dpus += len(rank)
    return rank_partitioning

def grouper(iterable, n=NR_REQUESTS_IN_BATCH, fillvalue=bytearray(request_size)):
    args = [iter(iterable)] * n
    return zip_longest(*args, fillvalue=fillvalue)


print("Allocating %u DPUs" %(args.nr_mrams))
with dpu.DpuSet(nr_dpus = args.nr_mrams, async_mode = True, binary = args.dpu_program) as dpu_set:
    print("Creating rank partitioning")
    rank_partitioning = create_rank_partitioning(dpu_set)

    print("Loading MRAMs")
    if(args.skip_load == False):
        dpu_set.call(load_mrams, [rank_partitioning, args.mrams_path], async_mode = False)
    else:
        print("Warning: skipping mrams load")

    if args.input is not None:
        print("Computing")
        with open(args.input) as f_input:
            lines = f_input.readlines()
            inputs = []
            inputs_batch = []
            request_words_tab = []
            request_words_batch = []
            cnt = 0
            nb_batches = 0
            for line in lines:
                if line.startswith("$$file"):
                    continue
                request_words, input, not_ok = set_input(line)
                if not_ok:
                    sys.exit(-1)
                inputs.append(input)
                request_words_tab.append(request_words)
                cnt = cnt + 1
                if cnt == NR_REQUESTS_IN_BATCH:
                    inputs_batch.append(bytearray([item for sublist in inputs for item in sublist]))
                    request_words_batch.append(request_words_tab)
                    request_words_tab = []
                    inputs = []
                    cnt = 0
                    nb_batches = nb_batches + 1

            if len(inputs):
                while len(inputs) < NR_REQUESTS_IN_BATCH:
                    inputs.append(bytearray(request_size));
                inputs_batch.append(bytearray([item for sublist in inputs for item in sublist]))
                nb_batches = nb_batches + 1

            if len(request_words_tab):
                while len(request_words_tab) < NR_REQUESTS_IN_BATCH:
                    request_words_tab.append([]);
                request_words_batch.append(request_words_tab)

            outputs_per_input = [[[bytearray(response_size * max_responses * NR_REQUESTS_IN_BATCH) for dpu in rank] 
                for rank in dpu_set.ranks()] for i in range(nb_batches)]
            stats_per_input = [[bytearray(stats_size * NR_REQUESTS_IN_BATCH) for dpu in dpu_set] for i in range(nb_batches)]

            input_buffer = bytearray(int_size)
            struct.pack_into("<I", input_buffer, 0, NR_REQUESTS_IN_BATCH)
            dpu_set.copy("nr_request_in_batch_input", input_buffer)

            n_rq = 0
            for input, request_words, outputs, stats in zip(inputs_batch, request_words_batch, outputs_per_input, stats_per_input):
                print("\nComputing batch %d of %d requests" % (n_rq, NR_REQUESTS_IN_BATCH), flush=True)
                compute(dpu_set, outputs, stats, input, rank_partitioning, n_rq)
                n_rq = n_rq + 1
            dpu_set.sync()
            dpu_set.call(output_responses_batch, [outputs_per_input, stats_per_input, request_words_batch, rank_partitioning, sys.stdout], is_blocking = True, single_call = True)
            print("Compute completed!", flush=True)
            dpu_set.sync()
    else:
        NR_REQUESTS_IN_BATCH = 1
        input_buffer = bytearray(int_size)
        struct.pack_into("<I", input_buffer, 0, NR_REQUESTS_IN_BATCH)
        dpu_set.copy("nr_request_in_batch_input", input_buffer)
        outputs = [[bytearray(response_size * max_responses) for dpu in rank] for rank in dpu_set.ranks()]
        stats = [bytearray(stats_size) for dpu in dpu_set]
        while True:
            n_rq = 0
            request_words, input_buffer = set_input_from_stdin()
            compute(dpu_set, outputs, stats, input_buffer, rank_partitioning, n_rq)
            dpu_set.call(output_responses, [outputs, stats, rank_partitioning, request_words, sys.stdout], is_blocking = True, single_call = True)
            dpu_set.sync()
            n_rq = n_rq + 1
