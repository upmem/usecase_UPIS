#!/usr/bin/env python3

import os
import argparse
import re
import tqdm

parser = argparse.ArgumentParser(
    description='')
parser.add_argument(
    "sources",
    metavar="<source_folder>",
    help="")
parser.add_argument(
    "output",
    metavar="<output_folder>",
    help="")
args = parser.parse_args()

def double_crochet_replace_fct(matchobj):
    return " " + matchobj.group(0).strip("[]").split("|")[-1]

def crochet_replace_fct(matchobj):
    return " " + " ".join(matchobj.group(0).split(" ")[1:])

file_id = 0
source_dir = os.path.abspath(args.sources)
source_dir_re = re.compile(source_dir)
subs_string = [
    ("\[\[([^\[\]]*)\]\]", double_crochet_replace_fct),
    ("\[[^\[\]]*\]", crochet_replace_fct),
    ("{{[^{}]*}}", ""),
    ("<[^<>]*>", ""),
]
subs_re = [(re.compile(regexp), replace) for (regexp, replace) in subs_string]
new_line_re = (re.compile("\."), ".\n")
word_filter_re = (re.compile("[^a-zA-Z ,\.\n()0-9]"), "")
title_re = (re.compile("[^a-zA-Z0-9]"), "_")

nb_files = 0
for subdir, dirs, files in os.walk(args.sources):
    for filename in files:
        nb_files += 1

with tqdm.tqdm(range(nb_files), ascii=True) as progress_bar:
    for subdir, dirs, files in os.walk(source_dir):
        for filename in files:
            progress_bar.update(1)
            input_filename = subdir + os.sep + filename
            with open(input_filename, "r") as input_file:
                lines = input_file.readlines()
                if len(lines) < 4:
                    continue
                title = lines[2]
                line = lines[4]
                filename = re.sub(title_re[0], title_re[1], title[:-1]) + ".txt"
                if len(filename) > 128:
                    continue
                output_filename = args.output + os.path.dirname(re.sub(source_dir_re, "", input_filename)) + os.sep + filename
                os.makedirs(os.path.dirname(output_filename), exist_ok = True)
                with open(output_filename, "w") as output_file:
                    for regexp, replace in subs_re:
                        line = re.sub(regexp, replace, line)
                    for regexp, replace in subs_re:
                        line = re.sub(regexp, replace, line)
                    output_file.write(re.sub(new_line_re[0],
                                             new_line_re[1],
                                             re.sub(word_filter_re[0],
                                                    word_filter_re[1],
                                                    line)))

