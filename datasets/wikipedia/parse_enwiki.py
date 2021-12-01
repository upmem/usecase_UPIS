#!/usr/bin/env python3

import xml.etree.ElementTree as ET
import sys
import argparse
import re
import subprocess
import bz2
import tqdm
import os

parser = argparse.ArgumentParser(
    description='')
parser.add_argument(
    "index",
    metavar="<index_file>",
    help="")
parser.add_argument(
    "source",
    metavar="<source_file>",
    help="")
args = parser.parse_args()

title_re = (re.compile("[^a-zA-Z0-9]"), "_")

blocks = [[0, 0]]
previous_start_block = 0
for line in open(args.index, "r").readlines():
    line_splitted = line.split(":")
    start_block = int(line_splitted[0])
    if start_block == previous_start_block:
        continue
    previous_block = blocks[-1]
    previous_block[1] = start_block - previous_block[1]
    blocks.append([int(start_block), int(start_block)])
    previous_start_block = start_block

blocks.pop(0)
source_file = open(args.source, "rb")
source_file.seek(0, 2)
last_block = blocks[-1]
last_block[1] = source_file.tell() - last_block[1]

with tqdm.tqdm(range(len(open(args.index, "r").readlines())), ascii=True) as progress_bar:
    for block_id, block in enumerate(blocks):
        source_file.seek(block[0], 0)
        prefix = "files/"
        while block_id > 10:
            prefix = prefix + str(block_id % 10) + "/"
            block_id = int(block_id / 10)
        os.makedirs(prefix, exist_ok = True)
        xml = "<mediawiki>\n" + bz2.decompress(source_file.read(block[1])).decode("utf-8")
        suffix = ""
        if xml.rfind("</mediawiki>") == -1:
            suffix = "</mediawiki>\n"
        for page in ET.fromstring(xml + suffix).findall("page"):
            filename = prefix + re.sub(title_re[0], title_re[1], page.find("title").text) + ".txt"
            try:
                with open(filename, "w") as output_file:
                    data = page.find("revision/text").text
                    output_file.write(data)
            except Exception as e:
                print("Could not create file '" + filename + "' (" + str(e) + ")")
            progress_bar.update(1)

source_file.close()

