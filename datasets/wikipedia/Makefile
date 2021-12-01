NB_DPUS=2450
SHARED_DIRECTORY=/home/shared/index_search/wikipedia
INDEX_DIRECTORY=${SHARED_DIRECTORY}
NB_REQUESTS_IN_BATCH=128

.PHONY: help build requests requests_with_answers

help:
	@echo '-------------------------------------------------------------------------------'
	@echo 'PIM Index Search on Wikipedia. Targets: '
	@echo '-------------------------------------------------------------------------------'
	@echo 'index:			run indexing program to create index for wikipedia files'
	@echo 'requests:		run host program to send batches of requests to DPUs'
	@echo 'requests_matches:	run host program to send batches of requests to DPUs, and display matches found'
	@echo '-------------------------------------------------------------------------------'

build:
	make -C ../..

index: build
	../../tools/index_builder/index_builder_cpp --dictionary_file_name=dictionary/dict.txt --input_directory_name=${SHARED_DIRECTORY}/files --nb_mrams=${NB_DPUS}  \
	--output_file_prefix=index --assign_strategy=file_size

requests: build
	@if [ ${NB_REQUESTS_IN_BATCH} -gt 128 ] ; then \
		echo 'Error: cannot set the number of requests per batch to be greater than 128'; \
	else \
		echo 'Copy the index (done only once) from shared area ${SHARED_DIRECTORY}'; \
		cp -R -u -p ${INDEX_DIRECTORY}/index ./ ; \
		echo 'Send batches of ${NB_REQUESTS_IN_BATCH} requests to DPUs'; \
		../../build/host_app -p ./index -m ${NB_DPUS} -r requests/requests.txt -d dictionary/dict.txt -b ${NB_REQUESTS_IN_BATCH}; \
	fi

requests_matches: build
	@if [ ${NB_REQUESTS_IN_BATCH} -gt 128 ] ; then \
		echo 'Error: cannot set the number of requests per batch to be greater than 128'; \
	else \
		echo 'Copy the index (done only once) from shared area ${SHARED_DIRECTORY}'; \
		cp -R -u -p ${INDEX_DIRECTORY}/index ./ ; \
		echo 'Send batches of ${NB_REQUESTS_IN_BATCH} requests to DPUs'; \
		../../build/host_app -p ./index -m ${NB_DPUS} -r requests/requests.txt -d dictionary/dict.txt -t -b ${NB_REQUESTS_IN_BATCH}; \
	fi


