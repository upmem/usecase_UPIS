INDEX_BUILDER_BIN=index_builder
INDEX_BUILDER_BIN_CPP=index_builder_cpp
INDEX_EXTRACT_BIN_CPP=index_extract

all: ${INDEX_BUILDER_BIN} ${INDEX_BUILDER_BIN_CPP} ${INDEX_EXTRACT_BIN_CPP}

${INDEX_BUILDER_BIN}: index_builder.c ../dict_parser/parser.c
	gcc -O3 -Wall -Werror -Wextra -o $@ $^ -lpthread

${INDEX_BUILDER_BIN_CPP}: index_builder.cpp
	g++ -O3 -std=c++17 -Werror -Wextra -o $@ $^ -g -L/usr/lib/x86_64-linux-gnu/lib -pthread -lstdc++fs

${INDEX_EXTRACT_BIN_CPP}: index_extract.cpp
	g++ -O3 -std=c++17 -Werror -Wextra -o $@ $^ -g -L/usr/lib/x86_64-linux-gnu/lib -pthread -lstdc++fs

clean:
	rm -rf ${INDEX_BUILDER_BIN} ${INDEX_BUILDER_BIN_CPP}
