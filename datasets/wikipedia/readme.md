# Wikipedia Index Search

This file describes the commands for indexing and searching the wikipedia dataset using PIM Index Search.
Note: indexing takes long time. If you are using this program in UPMEM cloud, it is not necessary to perform 
indexing as the index is already available in a
shared area '/home/shared/index_search/wikipedia/index'.
The index of the shared area is used by default.
It is copied on first execution in the current directory.

# Commands list

```
make index 
```
Use PIM indexing program to create an index for wikipedia files distributed over DPUs.
If you are using this program in UPMEM's cloud, the wikipedia files are located 
in a shared area `/home/shared/index_search/wikipedia`, no need to download them.
In case needed, see the instructions later in this file.
The default is to use 2450 DPUs, so the index is split into 2450 files.
This can be changed if needed by setting variable `NB_DPUS` in the Makefile.

```
make requests
```
Use PIM to search for a set of 2445 exact queries on the wikipedia dataset.
Queries are listed in the input file `requests/requests.txt`.
They all consist of sentences of 1 to 5 words, and they all have matches.
The total time to perform the requests is reported.
Requests are sent by batch of 128,
this can be changed by setting variable `NB_REQUESTS_IN_BATCH` in the Makefile.
This variable must be set at a value lower than 128, which is a static limit in the DPU code.
By default the index of the shared area is used. In order to help reducing the loading time,
the index is first copied in the current directory (if it does not already exists).

```
make requests_matches
```
Same as `make requests` but also displays the total number of matches as well as 
the location (doc ID, position) of the first match for each request.
Doc IDs are specified in the file `index/docs.txt`.

# To download the wikipedia dataset:

1) Dowload the following:


[Database](https://dumps.wikimedia.org/enwiki/latest/enwiki-latest-pages-articles-multistream.xml.bz2)


[Database_index](https://dumps.wikimedia.org/enwiki/latest/enwiki-latest-pages-articles-multistream-index.txt.bz2)
 

2) Decompress Database_index (not Database).


3) Generate database files using `<index_search>/datasets/wikipedia/parse_enwiki.py`.

