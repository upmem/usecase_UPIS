
.PHONY: all

INDEX_SEARCH_NB_DPUS ?= 64

all:
	../../tools/index_builder/index_builder_cpp --input_directory_name=files --output_file_prefix=mrams --dictionary_file_name=dict.txt --nb_mrams=${INDEX_SEARCH_NB_DPUS}
	../../tools/index_builder/index_builder_cpp --input_directory_name=files --output_file_prefix=mrams_lb --dictionary_file_name=dict.txt --nb_mrams=${INDEX_SEARCH_NB_DPUS} --assign_strategy=file_size
	python3 ../../host/main.py --dpu-program ../../build/dpu_task --mrams-path mrams --nr-mrams ${INDEX_SEARCH_NB_DPUS} --input requests.txt
	python3 ../../host/main.py --dpu-program ../../build/dpu_task --mrams-path mrams_lb --nr-mrams ${INDEX_SEARCH_NB_DPUS} --input requests.txt

