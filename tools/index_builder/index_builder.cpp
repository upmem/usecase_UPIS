/*
 * Copyright (c) 2021 - UPMEM
 * This file is part of a project which is released under MIT license.
 * See LICENSE file for details.
 */

/**
 * index_builder
 * Program to generate index from set of files
 *
 **/

#include "CmdParser.h"
#include <algorithm>
#include <assert.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <math.h>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

static constexpr unsigned NB_TASKLETS = 16;
static constexpr unsigned NB_THREADS = 16;

/**
 * Convert a string to lower case
 **/
static void toLower(std::string &s)
{

    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
}

/**
 * @return true is the string contains only alphabetic letters
 **/
static bool isAlpha(const std::string &s)
{

    return std::find_if(s.begin(), s.end(), [](char c) { return !std::isalpha(c); }) == s.end();
}

/**
 * @class Logger
 * singleton class to handle debug traces
 **/
class Logger {

public:
    // accessor
    static Logger &Get()
    {

        static Logger instance;
        return instance;
    }

    enum DebugMode { NONE, FILEOUTPUT, STDOUTPUT };
    void SetInfoMode(DebugMode m);
    void SetErrorMode(DebugMode m);

    void LogInfo(const std::string &msg) const;
    void LogInfo(std::function<void(std::ostream &)>) const;
    void LogError(const std::string &msg) const;
    void LogError(std::function<void(std::ostream &)>) const;

    // delete copy and move constructors and assign operators
    Logger(Logger const &) = delete; // Copy construct
    Logger(Logger &&) = delete; // Move construct
    Logger &operator=(Logger const &) = delete; // Copy assign
    Logger &operator=(Logger &&) = delete; // Move assign

private:
    // private constructor
    Logger()
        : _infoMode(NONE)
        , _errorMode(NONE)
        , _osInfo(0)
        , _osErr(0)
    {
    }

    static constexpr char _infoFileName[] { "info.txt" };
    static constexpr char _errFileName[] { "error.txt" };
    DebugMode _infoMode;
    DebugMode _errorMode;
    std::ostream *_osInfo;
    std::ostream *_osErr;
    std::unique_ptr<std::ofstream> _ofsInfo;
    std::unique_ptr<std::ofstream> _ofsErr;
};

void Logger::SetInfoMode(DebugMode m)
{

    switch (m) {

    case NONE:
        _osInfo = nullptr;
        break;
    case FILEOUTPUT:
        if (!_ofsInfo) {
            _ofsInfo = std::make_unique<std::ofstream>(_infoFileName);
        }
        _osInfo = _ofsInfo.get();
        break;
    case STDOUTPUT:
        _osInfo = &std::cout;
        break;
    }
    _infoMode = m;
}

void Logger::SetErrorMode(DebugMode m)
{

    switch (m) {

    case NONE:
        _osErr = nullptr;
        break;
    case FILEOUTPUT:
        if (!_ofsErr) {
            _ofsErr = std::make_unique<std::ofstream>(_errFileName);
        }
        _osErr = _ofsErr.get();
        break;
    case STDOUTPUT:
        _osErr = &std::cerr;
        break;
    }
    _errorMode = m;
}

void Logger::LogInfo(const std::string &msg) const
{

    if (_infoMode != NONE) {
        assert(_osInfo);
        *_osInfo << msg;
    }
}

void Logger::LogError(const std::string &msg) const
{

    if (_errorMode != NONE) {
        assert(_osErr);
        *_osErr << msg;
    }
}

void Logger::LogInfo(std::function<void(std::ostream &)> f) const
{

    if (_infoMode != NONE) {
        assert(_osInfo);
        f(*_osInfo);
    }
}

void Logger::LogError(std::function<void(std::ostream &)> f) const
{

    if (_errorMode != NONE) {
        assert(_osErr);
        f(*_osErr);
    }
}

/**
 * @class InputTxtFileParser
 * singleton class to hold dictionary and parse input text files
 **/
class InputTxtFileParser {

public:
    // accessor
    static InputTxtFileParser &Get()
    {

        static InputTxtFileParser instance;
        return instance;
    }

    bool ReadDictionary(const std::string &fileName)
    {

        bool res = ReadWords(fileName, [this](const std::string &s, unsigned) { _dict.insert(s); });

        _wordIndex.clear();
        _wordCount.resize(_dict.size(), 0);
        unsigned i = 0;
        for (auto const &e : _dict)
            _wordIndex[e] = i++;

        return res;
    }

    void DumpDictionary(std::ofstream &ofs) const
    {

        size_t i = 0;
        for (auto const &e : _dict)
            ofs << i++ << " " << e << "\n";
    }

    /**
     * Read input files checking for words in dictionary
     * Uses a callback on each word providing the string, position in the file
     * and word id (from dictionary). In case the word is unknown the word id is
     * set to max unsigned value (INVALID).
     **/
    bool ReadFile(const std::string &fileName, std::function<void(const std::string &s, unsigned, unsigned)> cb)
    {

        return ReadWords(fileName, [this, cb](const std::string &s, unsigned pos) {
            unsigned wid = INVALID;
            if (isAlpha(s)) {
                auto it = _wordIndex.find(s);
                if (it != _wordIndex.end())
                    wid = it->second;
            }
            if (wid != INVALID)
                _wordCount[wid]++;

            cb(s, pos, wid);
        });
    }

    /**
     * Dictionary accessor
     **/
    const std::set<std::string> &Dict() const { return _dict; }

    /**
     * Internally this class keeps word count of each word in dictionary
     * The word count is incremented each time the word is encountered in ReadFile function
     **/
    uint64_t GetWordCount(const std::string &w) const
    {

        auto it = _wordIndex.find(w);
        if (it != _wordIndex.end())
            return _wordCount[it->second];
        return 0;
    }

    void DumpWordCount(std::ofstream &ofs) const
    {

        size_t i = 0;
        for (auto const &e : _wordCount)
            ofs << i++ << " " << e << "\n";
    }

    // delete copy and move constructors and assign operators
    InputTxtFileParser(InputTxtFileParser const &) = delete; // Copy construct
    InputTxtFileParser(InputTxtFileParser &&) = delete; // Move construct
    InputTxtFileParser &operator=(InputTxtFileParser const &) = delete; // Copy assign
    InputTxtFileParser &operator=(InputTxtFileParser &&) = delete; // Move assign

    static constexpr unsigned INVALID = std::numeric_limits<unsigned>::max();

private:
    // private constructor
    InputTxtFileParser() { }

    static bool ReadWords(const std::string &fileName, std::function<void(const std::string &, unsigned)> cb);

    std::set<std::string> _dict;
    std::map<std::string, unsigned> _wordIndex;
    std::vector<uint64_t> _wordCount;
};

/**
 * Auxiliary function to read words from a file and apply a callback on each word
 **/
bool InputTxtFileParser::ReadWords(const std::string &fileName, std::function<void(const std::string &, unsigned)> cb)
{

    std::ifstream ifs(fileName);
    std::string word;
    unsigned pos = 0;
    while (ifs >> word) {
        toLower(word);
        cb(word, pos++);
    }
    return !ifs.fail() || ifs.eof();
}

/**
 * @class Delta
 * Computes a series of differences between consecutive values
 * Values must be passed in increasing order
 **/
template <typename T> class Delta {

public:
    Delta()
        : _first(true)
        , _curr()
    {
    }

    T Get(const T &e)
    {

        assert((e > _curr) || (_first && e == _curr));
        T delta = e - _curr;
        _curr = e;
        _first = false;
        return delta;
    }

private:
    bool _first;
    T _curr;
};

/**
 * @class CompressedInteger
 * Used to write a compressed integer to ofstream
 * The number is represented by several bytes, each
 * having 7 significant bits and the last bit used to
 * specify if the next byte is for the same number or the
 * next one (1 if this is the last byte).
 * Ex: TODO
 **/
class CompressedInteger {

public:
    /**
     * Constructors
     **/
    explicit CompressedInteger() { }
    explicit CompressedInteger(unsigned v);

    /**
     * Concatenate the representation of several integer numbers
     * After this operation this class actually holds a series of integers
     **/
    void Concatenate(unsigned v);
    void Concatenate(const CompressedInteger &v);

    /**
     * dump without building the object and storing the value
     * @return number of bytes written
     **/
    static unsigned DumpCompressedInteger(unsigned v, std::ofstream &ofs);

    size_t ByteSize() const { return _byteArray.size(); }

    friend std::ofstream &operator<<(std::ofstream &ofs, const CompressedInteger &v);

private:
    friend class CompressedIntegerPattern;

    static void BuildCompressedValue(unsigned v, std::function<void(char c)> cb);

    std::vector<char> _byteArray;
};

CompressedInteger::CompressedInteger(unsigned v) { Concatenate(v); }

void CompressedInteger::Concatenate(unsigned v)
{

    BuildCompressedValue(v, [this](char c) { _byteArray.push_back(c); });
    assert(_byteArray.size() && (!v || (_byteArray.back() & 0xff) > 0x80));
}

void CompressedInteger::Concatenate(const CompressedInteger &v)
{

    _byteArray.insert(_byteArray.end(), v._byteArray.begin(), v._byteArray.end());
}

unsigned CompressedInteger::DumpCompressedInteger(unsigned v, std::ofstream &ofs)
{

    unsigned byteSize = 0;
    BuildCompressedValue(v, [&ofs, &byteSize](char c) {
        ofs.write(&c, sizeof(char));
        byteSize++;
    });
    return byteSize;
}

void CompressedInteger::BuildCompressedValue(unsigned v, std::function<void(char)> cb)
{

    unsigned val = v;

    // each byte holds 7 bits of val and most significant bit
    // tells if the next byte is for the same value or next
    while (val) {

        char curr = val & 0x7f;
        if (val < 0x80) {
            curr |= 0x80;
        }
        cb(curr);
        val >>= 7;
    }
    if (!v) { // special handling for 0
        cb(0x80);
    }
}

std::ofstream &operator<<(std::ofstream &ofs, const CompressedInteger &v)
{

    ofs.write(v._byteArray.data(), v._byteArray.size());
    return ofs;
}

/**
 * @class CompressedIntegerPattern
 * Pattern of integers to be dumped as CompressedInteger multiple times
 **/
class CompressedIntegerPattern {

public:
    CompressedIntegerPattern(const std::vector<unsigned> &pattern)
    {

        CompressedInteger tmp;
        for (auto const &e : pattern)
            tmp.Concatenate(e);
        _pattern = tmp._byteArray;
    }

    /**
     * @return number of bytes written
     **/
    unsigned DumpPattern(std::ofstream &ofs, unsigned ntimes);

private:
    std::vector<char> _pattern;
};

unsigned CompressedIntegerPattern::DumpPattern(std::ofstream &ofs, unsigned ntimes)
{

    for (unsigned i = 0; i < ntimes; ++i)
        ofs.write(_pattern.data(), _pattern.size());

    return ntimes * _pattern.size();
}

/**
 * @class IndexSizeInfo
 * Keeps statistics over index size (min, max, sd etc)
 **/
class IndexSizeInfo {

public:
    IndexSizeInfo() { }

    void AddSize(unsigned sz);

    uint64_t GetTotalSize() const { return _totalSize; }
    unsigned GetMaxSize() const { return _maxSize; }
    unsigned GetMinSize() const { return _minSize; }

    float GetAverage() const
    {
        if (!_count)
            return 0.0f;
        return ((double)_totalSize) / _count;
    }
    float GetStdDev() const
    {
        if (!_count)
            return 0.0f;
        return sqrt(((double)_runningSd) / _count);
    }

private:
    std::mutex _mut;
    uint64_t _totalSize { 0 };
    unsigned _maxSize { 0 };
    unsigned _minSize { std::numeric_limits<unsigned>::max() };
    unsigned _count { 0 };
    double _runningAvg { 0.0 };
    double _runningSd { 0.0 };
};

void IndexSizeInfo::AddSize(unsigned sz)
{

    std::lock_guard<std::mutex> lock(_mut);

    _totalSize += sz;
    _maxSize = std::max(_maxSize, sz);
    _minSize = std::min(_minSize, sz);
    _count++;
    double d = sz - _runningAvg;
    _runningAvg += ((double)sz - _runningAvg) / _count;
    _runningSd += d * ((double)sz - _runningAvg);
}

/**
 * @class FileIndex
 * Object model of file index
 **/
class FilesIndex {

public:
    FilesIndex()
        : _nbFiles(0)
    {
    }

    bool AddFile(const std::string &fileName, unsigned fileId);

    /**
     * Dump file index
     * @return number of bytes / number of word positions written
     **/
    std::pair<unsigned, unsigned> Dump(
        std::ofstream &ofs, unsigned nextFileID, std::function<void(unsigned, unsigned)> offsetCB) const;

    unsigned NbFiles() const { return _nbFiles; }

private:
    unsigned _nbFiles;
    std::map<unsigned, std::map<unsigned, std::vector<unsigned>>> _word2posMap;
};

bool FilesIndex::AddFile(const std::string &fileName, unsigned fileId)
{

    bool status = InputTxtFileParser::Get().ReadFile(fileName, [this, fileId](const std::string &s, unsigned pos, unsigned wid) {
        if (wid != InputTxtFileParser::INVALID)
            _word2posMap[wid][fileId].push_back(pos);
    });
    if (!status) {
        std::ostringstream oss;
        oss << "Error: could not parse file " << fileName << "\n";
        Logger::Get().LogError(oss.str());
    } else
        _nbFiles++;
    return status;
}

std::pair<unsigned, unsigned> FilesIndex::Dump(
    std::ofstream &ofs, unsigned nextFileID, std::function<void(unsigned, unsigned)> offsetCB) const
{

    static constexpr unsigned SEG_SIZE = 100;

    CompressedInteger zero(0);
    CompressedInteger nfid(nextFileID);
    CompressedIntegerPattern absWordPattern({ nextFileID, 0, 0, nextFileID, 0 });
    auto const &dict = InputTxtFileParser::Get().Dict();
    unsigned wid = 0;
    unsigned nbBytes = 0;
    unsigned nbPos = 0;

    // for each word
    for (auto const &w : _word2posMap) {

        // for each word in dict but not in file index store
        // the position in offsetVec and dump next file ID as the end marker
        unsigned tmp = wid;
        while (wid < w.first) {
            offsetCB(wid, nbBytes);
            ++wid;
        }
        assert(wid < dict.size() && "Error: invalid word in file index (not in dict)");
        if (wid > tmp)
            nbBytes += absWordPattern.DumpPattern(ofs, wid - tmp);

        auto const &files = w.second;

        // store byte offset of each word
        offsetCB(wid, nbBytes);
        ++wid;

        // first dump last file ID
        ofs << nfid;
        nbBytes += nfid.ByteSize();

        // for each file
        Delta<unsigned> deltaDID;
        unsigned nbFiles = SEG_SIZE;
        for (auto const &f : files) {

            // dump zero values for NEXTDID and SEG_SIZE fields
            // this is dumped every SEG_SIZE files
            if (nbFiles == SEG_SIZE) {
                ofs << zero << zero;
                nbBytes += 2 * zero.ByteSize();
                nbFiles = 0;
            }
            nbFiles++;

            // dump delta DID
            nbBytes += CompressedInteger::DumpCompressedInteger(deltaDID.Get(f.first), ofs);

            // for each word occurence
            Delta<unsigned> deltaPos;
            std::vector<std::unique_ptr<CompressedInteger>> woVec;
            woVec.reserve(f.second.size());
            nbPos += f.second.size();
            size_t len = 0;
            for (auto const &wo : f.second) {

                woVec.push_back(std::make_unique<CompressedInteger>(deltaPos.Get(wo)));
                len += (woVec.back())->ByteSize();
            }
            // first dump bytes length then dump values
            nbBytes += CompressedInteger::DumpCompressedInteger(len, ofs);
            for (auto const &c : woVec) {
                ofs << *c;
                nbBytes += c->ByteSize();
            }
        }

        // start a new section to connect with next
        if (nbFiles == SEG_SIZE) {
            ofs << zero << zero;
            nbBytes += 2 * zero.ByteSize();
        }
        nbBytes += CompressedInteger::DumpCompressedInteger(deltaDID.Get(nextFileID), ofs);
        ofs << zero;
        nbBytes += zero.ByteSize();
    }

    // handle last words, those in dict but not in file index
    unsigned tmp = wid;
    while (wid < dict.size()) {
        offsetCB(wid, nbBytes);
        ++wid;
    }
    if (wid > tmp)
        nbBytes += absWordPattern.DumpPattern(ofs, wid - tmp);

    return std::make_pair(nbBytes, nbPos);
}

/**
 * @class MramIndexFile
 * Object model to create, store and dump the index file objects for a given MRAM
 * An Mram has a file index for each tasklet
 **/
class MramIndexFile {

public:
    MramIndexFile(unsigned mramid, const std::vector<std::string> &fileNames,
        const std::vector<std::pair<unsigned, unsigned>> &taskletStartEndID);

    /**
     * write MRAM index file
     * @return number of bytes written
     **/
    unsigned Dump(std::ofstream &ofs) const;

    /**
     * @method createAndDump
     * This is the method passed to std::async for thread-based implementation
     * Repetively take the next MRAM id (atomic), build
     * the MramIndexFile object and dump it
     **/
    static void createAndDump(unsigned tid, std::atomic_uint &nextMramID, IndexSizeInfo &indInfo,
        const std::vector<std::string> &fileNames,
        const std::vector<std::vector<std::pair<unsigned, unsigned>>> &taskletStartEndFileID, const std::string &outfilePrefix)
    {

        unsigned id;
        while ((id = std::atomic_fetch_add(&nextMramID, 1U)) < taskletStartEndFileID.size()) {

            Logger::Get().LogInfo([tid, id](std::ostream &os) {
                std::ostringstream ossP;
                ossP << ">> Thread " << tid << " (" << std::this_thread::get_id() << ") starts building mram " << id
                     << " index object model\n";
                os << ossP.str();
            });
            assert(id < taskletStartEndFileID.size());
            MramIndexFile index(id, fileNames, taskletStartEndFileID[id]);
            std::ostringstream oss;
            oss << outfilePrefix << "/" << id << ".bin";
            {
                Logger::Get().LogInfo([tid, id](std::ostream &os) {
                    std::ostringstream ossP;
                    ossP << ">> Thread " << tid << " (" << std::this_thread::get_id() << ") starts dumping mram " << id
                         << " index file\n";
                    os << ossP.str();
                });
                std::ofstream ofs(oss.str().c_str(), std::ios::binary);
                indInfo.AddSize(index.Dump(ofs));
            }
            resizeFile(oss.str(), MRAM_CAPACITY);
        }
    }

    static unsigned constexpr MRAM_CAPACITY = (62 << 20);

private:
    static bool resizeFile(const std::string &s, unsigned sz);

    unsigned _mramid;
    unsigned _firstFileID;
    std::vector<FilesIndex> _taskletIndex;
};

std::vector<std::unique_ptr<std::atomic_uint>> filesCheckVec;

MramIndexFile::MramIndexFile(unsigned mramid, const std::vector<std::string> &fileNames,
    const std::vector<std::pair<unsigned, unsigned>> &taskletStartEndID)
    : _mramid(mramid)
    , _firstFileID(0)
    , _taskletIndex(taskletStartEndID.size())
{

    assert(taskletStartEndID.size());
    _firstFileID = taskletStartEndID[0].first;
    if (_firstFileID >= fileNames.size()) {

        std::ostringstream oss;
        oss << ">> Warning: mram " << mramid << " is assigned no input files\n";
        Logger::Get().LogError(oss.str());
        return;
    }

    // dispatch files among the tasklets
    for (size_t id = 0; id < taskletStartEndID.size(); ++id) {

        unsigned lastID = taskletStartEndID[id].second;
        if (lastID > fileNames.size()) {
            std::ostringstream oss;
            oss << ">> Warning: tasklet " << id << " of mram " << mramid << " is assigned no input files\n";
            Logger::Get().LogError(oss.str());
            continue;
        }

        std::ostringstream oss;
        for (size_t j = taskletStartEndID[id].first; j < lastID; ++j) {

            oss << ">> mram " << _mramid << " tasklet " << id << ": adding file (id=" << j << ") " << fileNames[j] << "\n";
            _taskletIndex[id].AddFile(fileNames[j], j);

            // each file should be handled by only one tasklet/mram
            std::atomic_fetch_add(filesCheckVec[j].get(), 1U);
        }
        Logger::Get().LogInfo(oss.str());
    }
}

unsigned MramIndexFile::Dump(std::ofstream &ofs) const
{

    unsigned nbBytes = 0;
    unsigned len = 0;

    // first save some space for the header section
    unsigned sz = _taskletIndex.size() * sizeof(uint32_t);
    auto const &dict = InputTxtFileParser::Get().Dict();

    {
        std::vector<char> tmpBuffer(sz, 0);
        for (size_t i = 0; i < dict.size(); ++i) {

            ofs.write(tmpBuffer.data(), tmpBuffer.size());
            nbBytes += tmpBuffer.size();
        }
    }

    // TODO what happens if files id are not consecutive for each tasklet. Double check
    unsigned nextFileID = _firstFileID;
    unsigned tasklet = 0;
    unsigned nbTasklets = _taskletIndex.size();
    std::vector<uint32_t> offsetVec(dict.size() * nbTasklets);

    for (auto const &t : _taskletIndex) {

        nextFileID += t.NbFiles();
        auto p = t.Dump(ofs, nextFileID, [&offsetVec, nbTasklets, tasklet, nbBytes](unsigned wid, unsigned val) {
            unsigned index = wid * nbTasklets + tasklet;
            assert(index < offsetVec.size());
            offsetVec[index] = val + nbBytes;
        });
        nbBytes += p.first;
        len += p.second;
        tasklet++;
    }

    // update header using offsetVec
    ofs.seekp(0);
    ofs.write(reinterpret_cast<const char *>(offsetVec.data()), offsetVec.size() * sizeof(uint32_t));

    // go back to end of file and continue
    ofs.seekp(0, std::ios::end);

    // fill the rest of the file with zero to achieve exactly the MRAM capacity
    unsigned mramSize = nbBytes;
    if (mramSize > MRAM_CAPACITY) {
        std::ostringstream oss;
        oss << "Error: mram capacity exceeded.";
        Logger::Get().LogError(oss.str());
        assert(0 && "fatal error: MRAM capacity exceeded");
    }
    return len;
}

bool MramIndexFile::resizeFile(const std::string &s, unsigned sz)
{

    // resize file to MRAM capacity
    try {
        std::filesystem::path fspath = s;
        std::filesystem::resize_file(fspath, sz);
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << ">> ERROR: cannot resize file " << s << " : " << e.what() << "\n";
        return false;
    }
    return true;
}

void DistributeFiles(const std::vector<std::string> &fileNamesVec,
    std::vector<std::vector<std::pair<unsigned, unsigned>>> &assign, std::function<unsigned(unsigned)> GetNbFilesInMram,
    unsigned nbmrams, unsigned nbTasklets)
{

    unsigned mram_start = 0;
    for (unsigned i = 0; i < nbmrams; ++i) {
        unsigned nbFilesInMram = GetNbFilesInMram(i);
        unsigned nbFilesPerTasklet = nbFilesInMram / nbTasklets;
        unsigned restTasklets = nbFilesInMram - nbTasklets * nbFilesPerTasklet;
        unsigned tasklet_start = mram_start;
        for (unsigned j = 0; j < nbTasklets; ++j) {
            assign[i][j].first = tasklet_start;
            unsigned nbFilesInTasklet = nbFilesPerTasklet + (j < restTasklets);
            assign[i][j].second = tasklet_start + nbFilesInTasklet;
            tasklet_start += nbFilesInTasklet;
        }
        mram_start += nbFilesInMram;
    }
}

/**
 * Simple assignment of files to MRAM/Tasklets
 **/
std::vector<std::vector<std::pair<unsigned, unsigned>>> simpleAssignment(
    const std::vector<std::string> &fileNamesVec, unsigned nbmrams, unsigned nbTasklets)
{

    std::vector<std::vector<std::pair<unsigned, unsigned>>> res;
    res.resize(nbmrams);
    unsigned max = std::numeric_limits<unsigned>::max();
    for (auto &e : res)
        e.resize(nbTasklets, std::make_pair(max, max));

    unsigned nbFilesPerMram = fileNamesVec.size() / nbmrams;
    unsigned restMram = fileNamesVec.size() - nbFilesPerMram * nbmrams;
    DistributeFiles(
        fileNamesVec, res, [nbFilesPerMram, restMram](unsigned i) { return nbFilesPerMram + (i < restMram); }, nbmrams,
        nbTasklets);
    return res;
}

/**
 * Assignment of files to MRAM/Tasklets to balance the load based
 * on the file size
 * @return the set of files ids for each mram
 **/
std::vector<std::vector<std::pair<unsigned, unsigned>>> loadBalancingAssignment(
    std::vector<std::string> &fileNamesVec, unsigned nbmrams, unsigned nbTasklets)
{

    std::vector<std::vector<std::pair<unsigned, unsigned>>> res(nbmrams);
    unsigned max = std::numeric_limits<unsigned>::max();
    for (auto &e : res)
        e.resize(nbTasklets, std::make_pair(max, max));

    std::vector<std::pair<std::vector<unsigned>, unsigned>> mramAssgn(nbmrams);

    std::vector<unsigned> fileSize(fileNamesVec.size());

    std::atomic_uint fileIndex { 0 };
    std::vector<std::future<void>> futVec;
    for (size_t t = 0; t < NB_THREADS; ++t) {
        futVec.emplace_back(std::async(std::launch::async, [&fileNamesVec, &fileSize, &fileIndex, t]() {
            unsigned i = 0;
            while ((i = std::atomic_fetch_add(&fileIndex, 1U)) < fileNamesVec.size()) {
                std::ifstream ifs(fileNamesVec[i], std::ifstream::ate | std::ifstream::binary);
                size_t sz = ifs.tellg();
                fileSize[i] = sz;
            }
        }));
    }
    for (auto &f : futVec)
        f.wait();

    auto cmpSize = [&fileSize](unsigned v1, unsigned v2) { return fileSize[v1] < fileSize[v2]; };
    std::priority_queue<unsigned, std::vector<unsigned>, decltype(cmpSize)> filesQ(cmpSize);
    for (size_t i = 0; i < fileNamesVec.size(); ++i)
        filesQ.push(i);

    auto cmpMramSize = [&mramAssgn, nbTasklets](unsigned v1, unsigned v2) -> bool {
        assert(v1 < mramAssgn.size());
        assert(v2 < mramAssgn.size());
        auto &v1Asgn = mramAssgn[v1];
        auto &v2Asgn = mramAssgn[v2];
        bool v1NbFiles = v1Asgn.first.size() >= nbTasklets;
        bool v2NbFiles = v2Asgn.first.size() >= nbTasklets;
        if (v1NbFiles != v2NbFiles)
            return v1NbFiles;
        if (v1Asgn.second != v2Asgn.second)
            return v1Asgn.second > v2Asgn.second;
        return v1 > v2;
    };
    std::priority_queue<unsigned, std::vector<unsigned>, decltype(cmpMramSize)> mramsQ(cmpMramSize);
    for (size_t i = 0; i < nbmrams; ++i)
        mramsQ.push(i);
    while (!filesQ.empty()) {

        unsigned fid = filesQ.top();
        unsigned mramid = mramsQ.top();
        filesQ.pop();
        mramsQ.pop();
        assert(mramid < mramAssgn.size());
        mramAssgn[mramid].first.push_back(fid);
        assert(fid < fileSize.size());
        mramAssgn[mramid].second += fileSize[fid];
        mramsQ.push(mramid);
    }

    DistributeFiles(
        fileNamesVec, res, [&mramAssgn](unsigned i) { return mramAssgn[i].first.size(); }, nbmrams, nbTasklets);

    // reorganize files id by mram assignment
    std::vector<std::string> newFilesVec(fileNamesVec.size());
    size_t i = 0;
    size_t j = 0;
    for (auto &e : mramAssgn) {
        for (auto &e2 : e.first) {
            newFilesVec[i++] = std::move(fileNamesVec[e2]);
        }
    }
    fileNamesVec.swap(newFilesVec);

    return res;
}

/**
 * Enum class for assignement strategy
 * This controls the assignment of files to MRAM
 **/
enum class AssignmentStrategy {
    // simple assignment where each MRAM gets a equal number of files
    SIMPLE,
    // load balancing based on file size
    FILE_SIZE
};

std::istream &operator>>(std::istream &in, AssignmentStrategy &s)
{

    std::string token;
    in >> token;
    if (token == "simple")
        s = AssignmentStrategy::SIMPLE;
    else if (token == "file_size")
        s = AssignmentStrategy::FILE_SIZE;
    else
        in.setstate(std::ios_base::failbit);

    return in;
}

std::ostream &operator<<(std::ostream &oss, const AssignmentStrategy &s)
{

    switch (s) {

    case AssignmentStrategy::SIMPLE:
        oss << "simple";
        break;
    case AssignmentStrategy::FILE_SIZE:
        oss << "file_size";
        break;
    default:
        oss << "unkown";
        break;
    }
    return oss;
}

/**
 * @class ProgressBar
 * basic functionality to show elapsed time and completion percentage
 **/
template <typename T> class ProgressBar {

public:
    /**
     * Constructor
     * @param total the total count of tasks to be achieved
     * @param context a context object given to getProgress callback
     * @param getProgress the function to retrieve the current count of tasks
     * already executed
     * @param oneline if true erase the same line each time with new info
     **/
    ProgressBar(unsigned total, const T &context, std::function<unsigned(const T &ctxt)> getProgress, bool oneline)
        : _total(total)
        , _ctxt(context)
        , _getProgress(getProgress)
        , _keepGoing(true)
        , _oneline(oneline)
    {

        assert(_total);
        _startTime = std::chrono::steady_clock::now();
        _fut = std::async(std::launch::async, &ProgressBar::ShowProgress, this);
    }

    /**
     * Destructor
     * Stop the progress bar
     **/
    ~ProgressBar() { Stop(); }

    /**
     * Stop progress bar
     * Set flags to false
     * Wait for the thread to finish
     **/
    void Stop()
    {

        if (_keepGoing) {
            _keepGoing = false;
            _fut.wait();
        }
    }

private:
    /**
     * API to dump elapsed time and completion percentage
     **/
    void ShowProgress()
    {

        while (_keepGoing) {

            std::ostringstream oss;
            auto elapsed = std::chrono::steady_clock::now() - _startTime;
            oss << ">> elapsed time: ";
            auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            if (elapsedSec < 60)
                oss << elapsedSec << "s";
            else
                oss << std::chrono::duration_cast<std::chrono::minutes>(elapsed).count() << "m";

            int completion = (int)round(_getProgress(_ctxt) * 100.0 / _total);
            oss << " ; Completion: " << completion;

            if (completion >= 100) {
                oss << "\%    \n";
                std::cout << oss.str() << std::flush;
                Stop();
            } else {
                if (_oneline)
                    oss << "\%    \r";
                else
                    oss << "\%    \n";
                std::cout << oss.str() << std::flush;
                // this thread is awaken every 500ms to show progress
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
    }

    unsigned _total;
    const T &_ctxt;
    bool _keepGoing;
    bool _oneline;
    std::function<unsigned(const T &ctxt)> _getProgress;
    std::chrono::time_point<std::chrono::steady_clock> _startTime;
    std::future<void> _fut;
};

/**
 * Method to generate random requests of MAX_REQUEST_SIZE
 * @param fileNames the set of files to parse
 * @param percRq the percentage of requests to generate (wrt the total number of possible matching requests)
 * @param seed the seed for the random generator
 **/
void GenerateRndRequests(const std::vector<std::string> &fileNames, float percRq, unsigned seed)
{

    static constexpr unsigned MAX_REQUEST_SIZE = 5;

    srand(seed);

    // create several threads dumping each in a seperate file and merge the files
    std::atomic_uint fileIndex { 0 };
    std::vector<std::future<void>> futVec;
    for (size_t t = 0; t < NB_THREADS; ++t) {

        futVec.emplace_back(std::async(std::launch::async, [&fileNames, &fileIndex, t, percRq]() {
            std::ostringstream oss;
            oss << "requests" << t << ".txt";
            std::ofstream ofs(oss.str());

            // atomically pick next file to handle
            unsigned id = 0;
            while ((id = std::atomic_fetch_add(&fileIndex, 1U)) < fileNames.size()) {

                const std::string &f = fileNames[id];
                std::vector<std::string> sentence(MAX_REQUEST_SIZE);
                unsigned sindex = 0;
                std::ostringstream ossFile;
                std::string fileName = fileNames[id];
                bool firstSentence = true;
                bool status = InputTxtFileParser::Get().ReadFile(f,
                    [&sindex, &ossFile, &sentence, percRq, fileName, &firstSentence](
                        const std::string &s, unsigned pos, unsigned wid) {
                        if (wid == InputTxtFileParser::INVALID) {
                            // skip and reset
                            std::fill(sentence.begin(), sentence.end(), "");
                            sindex = 0;
                            return;
                        }
                        sentence[sindex++] = s;
                        if (sindex == MAX_REQUEST_SIZE)
                            sindex = 0;
                        unsigned sentenceSize
                            = std::count_if(sentence.begin(), sentence.end(), [](const std::string &s) { return s.size(); });
                        if (sentenceSize < 2)
                            return;
                        auto r = ((double)rand() / (RAND_MAX));
                        if (r < percRq) {
                            if (firstSentence)
                                ossFile << "$$file " << fileName << "\n";
                            firstSentence = false;
                            for (unsigned j = sindex; j < sentence.size(); ++j) {
                                if (sentence[j].size())
                                    ossFile << sentence[j] << " ";
                            }
                            for (unsigned j = 0; j < sindex; ++j) {
                                if (sentence[j].size())
                                    ossFile << sentence[j] << " ";
                            }
                            ossFile << "\n";
                        }
                    });
                ofs << ossFile.str() << std::flush;
            }
        }));
    }

    for (auto &f : futVec)
        f.wait();

    // merge files
    {
        std::ofstream ofs("requests.txt");
        for (size_t t = 0; t < NB_THREADS; ++t) {
            std::ostringstream oss;
            oss << "requests" << t << ".txt";
            std::ifstream ifs(oss.str());
            ofs << ifs.rdbuf() << std::flush;
            remove(oss.str().c_str());
        }
    }

    // parse the request file again to compute and dump the word count for each request
    {
        std::ifstream ifs("requests.txt");
        std::string line;
        std::ofstream ofs2("requests_wc.txt");
        while (std::getline(ifs, line)) {

            if (line.rfind("$$file", 0) == 0)
                continue;
            std::istringstream iss(line);
            std::string word;
            while (iss >> word) {

                uint64_t wc = InputTxtFileParser::Get().GetWordCount(word);
                assert(wc > 0);
                ofs2 << wc << " ";
            }
            ofs2 << std::endl;
        }
    }

    std::ofstream ofs3("wc.txt");
    InputTxtFileParser::Get().DumpWordCount(ofs3);
}

/**
 * Gather all inputs files using multiple threads to go through first level sub-directories
 **/
void ExploreInputDirectory(const std::string &dirname, std::vector<std::string> &fileNamesVec)
{

    // for (const auto & entry : std::filesystem::recursive_directory_iterator(dirname)) {
    //  if(!std::filesystem::is_regular_file(entry.status())) continue;
    //  fileNamesVec.push_back(entry.path().string());
    //}
    std::vector<std::string> dirnames;
    for (const auto &entry : std::filesystem::directory_iterator(dirname)) {
        if (std::filesystem::is_directory(entry.status())) {
            dirnames.push_back(entry.path().string());
            continue;
        } else if (!std::filesystem::is_regular_file(entry.status()))
            continue;
        fileNamesVec.push_back(entry.path().string());
    }
    std::atomic_uint dirIndex { 0 };
    std::vector<std::future<void>> futVec;
    std::vector<std::vector<std::string>> fNames(dirnames.size());
    for (unsigned i = 0; i < NB_THREADS; ++i) {
        futVec.emplace_back(std::async(std::launch::async, [&dirnames, &dirIndex, &fNames]() {
            unsigned id;
            while ((id = std::atomic_fetch_add(&dirIndex, 1U)) < dirnames.size()) {

                for (const auto &entry : std::filesystem::recursive_directory_iterator(dirnames[id])) {
                    if (!std::filesystem::is_regular_file(entry.status()))
                        continue;
                    fNames[id].push_back(entry.path().string());
                }
            }
        }));
    }

    // wait for the threads to finish
    for (auto &f : futVec)
        f.wait();

    for (auto const &vec : fNames) {

        fileNamesVec.insert(fileNamesVec.end(), std::make_move_iterator(vec.begin()), std::make_move_iterator(vec.end()));
    }
}

/**
 *
 *  Main program
 *
 **/
int main(int argc, char *argv[])
{

    std::cout << ">> Running file index builder\n" << std::flush;

    std::string dirname, outprefix, dictname;
    int nbmrams = 1;
    AssignmentStrategy asgnStrategy = AssignmentStrategy::SIMPLE;
    float rndReqPerc = 0.0f;
    bool debug = false;

    std::map<std::string, std::pair<bool, std::string>> validOpts
        = { { "--input_directory_name", { true, "directory containing the files to index" } },
              { "--output_file_prefix", { true, "prefix name for output files generated by this program" } },
              { "--dictionary_file_name", { true, "input file containing the dictionary of valid words" } },
              { "--nb_mrams", { true, "number of mram files to generate" } },
              { "--assign_strategy", { true, "strategy for files to mram assignment" } },
              { "--generate_random_requests",
                  { true, "generate random matching requests for testing with provided selection percentage" } },
              { "--help", { false, "displays help" } }, { "--debug", { false, "enable debug traces" } } };
    std::pair<std::string, std::string> cmdDescr("index_builder_cpp", "generates index for set of files");
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
    dictname = parser.GetCmdOption("--dictionary_file_name");
    if (dictname.empty()) {

        std::cerr << ">> error: dictionary_file_name is required\n";
        return -1;
    }
    std::cout << ">> Dictionary file name set to \"" << dictname << "\".\n";
    std::string nbmramsStr = parser.GetCmdOption("--nb_mrams");
    if (nbmramsStr.empty()) {

        std::cerr << ">> error: nb_mrams is required\n";
        return -1;
    } else {
        try {
            size_t pos;
            nbmrams = std::stoi(nbmramsStr, &pos);
            if (pos != nbmramsStr.size())
                throw std::exception();
        } catch (std::exception &e) {
            std::cerr << ">> error: please specify valid number of mrams (--nb_mrams)\n";
            return -1;
        }
        std::cout << ">> Number of mrams set to \"" << nbmrams << "\".\n";
    }
    std::string assignStr = parser.GetCmdOption("--assign_strategy");
    if (!assignStr.empty()) {
        if (assignStr.compare("file_size") == 0)
            asgnStrategy = AssignmentStrategy::FILE_SIZE;
        else if (assignStr.compare("simple")) {

            std::cerr << ">> error: invalid value " << assignStr << " for option --assign_strategy\n";
            return -1;
        }
        std::cout << ">> Assignement strategy set to \"" << asgnStrategy << "\".\n";
    }
    std::string rndStr = parser.GetCmdOption("--generate_random_requests");
    if (!rndStr.empty()) {
        try {
            size_t pos;
            rndReqPerc = std::stof(rndStr, &pos);
            if (pos != rndStr.size())
                throw std::exception();
        } catch (std::exception &e) {
            std::cerr << "error: please specify valid value to option --generate_random_requests\n";
            return -1;
        }
        if (rndReqPerc)
            std::cout << ">> Random requests generation activated with percentage \"" << rndReqPerc << "\".\n";
    }
    if (parser.CmdOptionExists("--debug")) {

        debug = true;
        Logger::Get().SetInfoMode(Logger::STDOUTPUT);
    }
    // enable error by default
    Logger::Get().SetErrorMode(Logger::STDOUTPUT);

    // create output directory
    try {
        std::filesystem::create_directory(outprefix);
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << ">> ERROR: " << e.what() << "\n";
        return 1;
    }

    if (!InputTxtFileParser::Get().ReadDictionary(dictname)) {
        std::cerr << ">> ERROR: cannot read dictionary file " << dictname << ".\n";
        return 1;
    } else {
        std::cout << ">> INFO: Found " << InputTxtFileParser::Get().Dict().size() << " case-insensitive words in dictionary "
                  << dictname << ".\n";
        // Dump dictionary for debug
        std::ofstream dictfs(outprefix + "/words.txt");
        InputTxtFileParser::Get().DumpDictionary(dictfs);
    }

    std::vector<std::string> fileNamesVec;

    std::cout << ">> INFO: Start looking for files in " << dirname << ".\n" << std::flush;

    try {
        ExploreInputDirectory(dirname, fileNamesVec);
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << ">> ERROR: " << e.what() << "\n";
        return false;
    }

    std::cout << ">> INFO: Found " << fileNamesVec.size() << " input files.\n" << std::flush;

    static constexpr unsigned seed = 0; // NOTE: change seed to get a different set of requests
    if (rndReqPerc) {
        std::cout << ">> INFO: generating random requests ..."
                  << "\n";
        GenerateRndRequests(fileNamesVec, rndReqPerc, seed);
        std::cout << ">> INFO: Done."
                  << "\n";
    }

    // assign files to each tasklet
    std::vector<std::vector<std::pair<unsigned, unsigned>>> taskletStartEndFileID;
    if (asgnStrategy == AssignmentStrategy::SIMPLE)
        taskletStartEndFileID = simpleAssignment(fileNamesVec, nbmrams, NB_TASKLETS);
    else if (asgnStrategy == AssignmentStrategy::FILE_SIZE) {
        std::cout << ">> INFO: Start computing files to mram assignment (file_size strategy).\n" << std::flush;
        taskletStartEndFileID = loadBalancingAssignment(fileNamesVec, nbmrams, NB_TASKLETS);
        std::cout << ">> INFO: Finished computing files assignment.\n" << std::flush;
    } else
        assert(0); // should not happen : unknown strategy

    // Log assignement of files to mrams
    Logger::Get().LogInfo([&taskletStartEndFileID](std::ostream &os) {
        for (uint32_t i = 0; i < taskletStartEndFileID.size(); ++i) {
            for (uint32_t j = 0; j < taskletStartEndFileID[i].size(); ++j) {
                os << "MRAM " << i << " tasklet " << j << ": start file " << taskletStartEndFileID[i][j].first << " end file "
                   << taskletStartEndFileID[i][j].second << "\n";
            }
        }
    });

    // Dump file ids for debug
    {
        std::ofstream fiofs(outprefix + "/docs.txt");
        for (size_t i = 0; i < fileNamesVec.size(); ++i) {

            fiofs << i << " " << fileNamesVec[i] << "\n";
        }
    }

    filesCheckVec.resize(fileNamesVec.size());
    for (unsigned i = 0; i < filesCheckVec.size(); ++i)
        filesCheckVec[i] = std::make_unique<std::atomic_uint>(0);

    // create NB_THREADS async tasks for mram files creation
    // the task is responsible for reading files allocated to DPU tasklets, create
    // the index and dump it in an output file
    std::vector<std::unique_ptr<std::ofstream>> outputFiles(nbmrams);
    std::vector<std::future<void>> futVec;
    std::atomic_uint nextMramID { 0 };
    IndexSizeInfo indInfo;
    for (unsigned i = 0; i < NB_THREADS; ++i) {

        futVec.emplace_back(std::async(std::launch::async, &MramIndexFile::createAndDump, i, std::ref(nextMramID),
            std::ref(indInfo), std::ref(fileNamesVec), std::ref(taskletStartEndFileID), std::ref(outprefix)));
    }

    // start progress bar
    ProgressBar<std::atomic_uint> pb(
        nbmrams, nextMramID, [nbmrams](const std::atomic_uint &i) { return std::min((unsigned)i, (unsigned)nbmrams); }, !debug);

    // wait for the threads to finish
    for (auto &f : futVec)
        f.wait();

    // stop the progress bar
    pb.Stop();

    // check each file has been associated to one unique tasklet
    for (unsigned i = 0; i < fileNamesVec.size(); ++i) {
        if (*filesCheckVec[i] != 1) {
            std::cout << "file " << i << " has been added " << *filesCheckVec[i] << " times\n" << std::flush;
        }
        assert(*filesCheckVec[i] == 1);
    }

    std::cout.precision(2);
    std::cout << std::scientific << ">> Total index size: " << (double)indInfo.GetTotalSize() << " positions\n";
    std::cout << std::scientific << ">> Index size std dev: " << indInfo.GetStdDev() << " positions\n";
    std::cout << std::scientific << ">> Index max size: " << (double)indInfo.GetMaxSize() << " positions\n";
    std::cout << std::scientific << ">> Index min size: " << (double)indInfo.GetMinSize() << " positions\n";

    return 0;
}
