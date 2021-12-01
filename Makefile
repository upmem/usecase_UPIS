BUILDDIR ?= build

HOST_BINARY=${BUILDDIR}/host_app
HOST_SOURCES=$(wildcard host/*.c)
DICT_SOURCES=$(wildcard tools/dict_parser/*.c)
HOST_HEADERS=$(wildcard host/*.h)

DPU_BINARY=${BUILDDIR}/dpu_task
DPU_SOURCES=$(wildcard dpu/src/*.c)
DPU_HEADERS=$(wildcard dpu/inc/*.h)

COMMONS_HEADERS=$(wildcard common/inc/*.h)

OUTPUT_FILE=${BUILDDIR}/output.txt
PLOTDATA_FILE=${BUILDDIR}/plotdata.csv

CHECK_FORMAT_FILES=${HOST_SOURCES} ${HOST_HEADERS} ${DPU_SOURCES} ${DPU_HEADERS} ${COMMONS_HEADERS}
CHECK_FORMAT_DEPENDENCIES=$(addsuffix -check-format,${CHECK_FORMAT_FILES})

NR_TASKLETS ?= 16

__dirs := $(shell mkdir -p ${BUILDDIR})

.PHONY: all clean run plotdata check check-format tools

all: ${HOST_BINARY} ${DPU_BINARY} tools
clean:
	rm -rf ${BUILDDIR}

###
### HOST APPLICATION
###
CFLAGS=-g -Wall -Werror -Wextra -O3 -std=c11 `dpu-pkg-config --cflags dpu` -Ihost/inc -Icommon/inc -Itools/dict_parser -DNR_TASKLETS=${NR_TASKLETS}
LDFLAGS=`dpu-pkg-config --libs dpu` -fopenmp

${HOST_BINARY}: ${HOST_SOURCES} ${HOST_HEADERS} ${COMMONS_HEADERS} ${DPU_BINARY}
	$(CC) -o $@ ${HOST_SOURCES} ${DICT_SOURCES} $(LDFLAGS) $(CFLAGS) -DDPU_BINARY=\"$(realpath ${DPU_BINARY})\"

###
### DPU BINARY
###
DPU_FLAGS=-g -O2 -Wall -Werror -Wextra -flto=thin -Idpu/inc -Icommon/inc -DNR_TASKLETS=${NR_TASKLETS} -DSTACK_SIZE_DEFAULT=256

${DPU_BINARY}: ${DPU_SOURCES} ${DPU_HEADERS} ${COMMONS_HEADERS}
	dpu-upmem-dpurte-clang ${DPU_FLAGS} ${DPU_SOURCES} -o $@

###
### EXECUTION & TEST
###
run: all
	${MAKE} -C datasets/integration > ${OUTPUT_FILE} 2>&1
	${MAKE} -C datasets/integration2 >> ${OUTPUT_FILE} 2>&1
	cat ${OUTPUT_FILE}

tools:
	${MAKE} -C tools/index_builder

check:
	cat ${OUTPUT_FILE} | grep "matchs found" | diff datasets/integration/output.txt -
	awk -f sumMatches.awk ${OUTPUT_FILE} | diff datasets/integration2/ref.txt -

plotdata:
	echo "Mcc" > ${PLOTDATA_FILE}
	cat ${OUTPUT_FILE} | grep "average execution time" | sed 's/\[DPU\]  average execution time.*= .* ms (\(.*\) Mcc)/\1/' >> ${PLOTDATA_FILE}

%-check-format: %
	clang-format $< | diff -y --suppress-common-lines $< -

check-format: ${CHECK_FORMAT_DEPENDENCIES}
