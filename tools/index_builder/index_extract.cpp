/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

/**
 * index_extract
 * Program to extract file index as text (for debug)
 *
 **/

#include "CmdParser.h"
#include <assert.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <vector>

uint32_t ReadCompressedInteger(std::ifstream &ifs, uint32_t &nbBytes)
{

    unsigned val = 0;
    uint32_t byte_shift = 0;
    uint8_t byte = 0;

    do {
        assert(byte_shift <= 29); // should not read more than 5 bytes (each byte has 7 significant bits for a 32 bit integer)
        byte = ifs.get();
        val = val | ((byte & 127) << byte_shift);
        byte_shift += 7;

    } while (!(byte & 128));

    nbBytes = byte_shift / 7;

    return val;
}

static constexpr unsigned SEG_SIZE = 100;

void ExtractIndexFile(std::string inputFile, std::string outputFile, int nbwords, int nbtasklets)
{

    std::ifstream ifs(inputFile, std::ios::binary);
    std::ofstream ofs(outputFile);

    std::vector<char> offsetVec(nbwords * nbtasklets * sizeof(uint32_t));

    ifs.read(offsetVec.data(), offsetVec.size());
    std::ostringstream oss;
    oss << "Extracting " << inputFile << " into " << outputFile << "\n";
    std::cout << oss.str() << std::flush;

    ofs << "Offset vector:\n";
    for (size_t i = 0; i < offsetVec.size(); i += 4) {

        assert(i + 3 < offsetVec.size());
        uint32_t v = *reinterpret_cast<uint32_t *>(offsetVec.data() + i);
        ofs << v << "\n";
    }

    ofs << "\n\n";

    uint32_t nbBytes = 0;
    for (int i = 0; i < nbtasklets; ++i) {
        ofs << "\nTasklet " << i << "\n" << std::flush;
        for (int j = 0; j < nbwords; ++j) {

            ofs << "\nWord " << j << "\n" << std::flush;
            uint32_t lastFileId = ReadCompressedInteger(ifs, nbBytes);
            ofs << "Last DID " << lastFileId << "\n";
            unsigned nbFiles = SEG_SIZE;
            unsigned currDid = 0;

            while (ifs) {

                if (nbFiles == SEG_SIZE) {
                    uint32_t a = ReadCompressedInteger(ifs, nbBytes);
                    uint32_t b = ReadCompressedInteger(ifs, nbBytes);
                    ofs << "New files segment\n" << std::flush;
                    assert(!a && !b);
                    nbFiles = 0;
                }
                nbFiles++;

                uint32_t ddid = ReadCompressedInteger(ifs, nbBytes);
                currDid += ddid;

                if (currDid == lastFileId) {

                    uint32_t a = ReadCompressedInteger(ifs, nbBytes);
                    assert(!a);
                    break;
                }

                ofs << "File DID " << currDid << "\n" << std::flush;

                int len = ReadCompressedInteger(ifs, nbBytes);
                ofs << "Length " << len << "\n";
                uint32_t pos = 0;
                while (len > 0) {

                    uint32_t dpos = ReadCompressedInteger(ifs, nbBytes);
                    len -= nbBytes;
                    pos += dpos;
                    ofs << pos << " (" << nbBytes << ") ";
                }
                assert(!len);
                ofs << "\n";
            }
        }
    }
}

static bool hasEnding(const std::string &fullString, const std::string &ending)
{

    if (fullString.length() >= ending.length()) {
        return (fullString.compare(fullString.length() - ending.length(), ending.length(), ending) == 0);
    } else {
        return false;
    }
}

int main(int argc, char *argv[])
{

    std::cout << ">> Running file index extract\n" << std::flush;

    std::string dirname, outprefix;
    int nbwords = 0, nbtasklets = 0;

    std::map<std::string, std::pair<bool, std::string>> validOpts
        = { { "--input_directory_name", { true, "directory containing the index to extract" } },
              { "--output_file_prefix", { true, "prefix name for output files generated by this program" } },
              { "--nb_words", { true, "number of words in dictionary used to generate this index" } },
              { "--nb_tasklets", { true, "number of tasklets used to generate this index" } },
              { "--help", { false, "displays help" } } };
    std::pair<std::string, std::string> cmdDescr("index_extract", "program to extract file index as text");
    CommandLineParser parser(argc, argv, cmdDescr, validOpts);

    if (parser.CmdOptionExists("--help")) {
        parser.PrintHelp();
        return 0;
    }
    std::string invalidOpt;
    if (!parser.IsValid(invalidOpt)) {

        std::cerr << ">> error: unknown option " << invalidOpt << ". Use --help.\n";
        return -1;
    }
    dirname = parser.GetCmdOption("--input_directory_name");
    if (dirname.empty()) {

        std::cerr << ">> error: input_directory_name is required\n";
        return -1;
    }
    std::cout << ">> Input directory name set to \"" << dirname << "\".\n";
    outprefix = parser.GetCmdOption("--output_file_prefix");
    if (outprefix.empty()) {

        std::cerr << ">> error: output_file_prefix is required\n";
        return -1;
    }
    std::cout << ">> Output file prefix set to \"" << outprefix << "\".\n";
    std::string nbwordsStr = parser.GetCmdOption("--nb_words");
    if (nbwordsStr.empty()) {

        std::cerr << ">> error: nb_words is required\n";
        return -1;
    } else {
        try {
            size_t pos;
            nbwords = std::stoi(nbwordsStr, &pos);
            if (pos != nbwordsStr.size())
                throw std::exception();
        } catch (std::exception &e) {
            std::cerr << ">> error: please specify valid number of words (--nb_words)\n";
            return -1;
        }
        std::cout << ">> Number of words set to \"" << nbwords << "\".\n";
    }
    std::string nbtaskletsStr = parser.GetCmdOption("--nb_tasklets");
    if (nbtaskletsStr.empty()) {

        std::cerr << ">> error: nb_tasklets is required\n";
        return -1;
    } else {
        try {
            size_t pos;
            nbtasklets = std::stoi(nbtaskletsStr, &pos);
            if (pos != nbtaskletsStr.size())
                throw std::exception();
        } catch (std::exception &e) {
            std::cerr << ">> error: please specify valid number of tasklets (--nb_tasklets)\n";
            return -1;
        }
        std::cout << ">> Number of tasklets set to \"" << nbtasklets << "\".\n";
    }

    // create output directory
    try {
        std::filesystem::create_directory(outprefix);
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << ">> ERROR: " << e.what() << "\n";
        return 1;
    }

    std::vector<std::future<void>> futVec;
    try {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(dirname)) {

            if (!std::filesystem::is_regular_file(entry.status()))
                continue;

            std::string fname = entry.path().string();
            if (hasEnding(fname, ".bin")) {
                std::string oname = outprefix + "/" + entry.path().filename().string();

                futVec.emplace_back(std::async(std::launch::async, &ExtractIndexFile, fname, oname, nbwords, nbtasklets));
            }
        }
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << ">> ERROR: " << e.what() << "\n";
        return 1;
    }

    for (auto &f : futVec)
        f.wait();
}