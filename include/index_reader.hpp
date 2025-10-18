#ifndef INDEX_READER_HPP
#define INDEX_READER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdint>
#include "varbyte.hpp"

// Term metadata
struct TermMeta {
    uint32_t df;              // document frequency
    uint64_t cf;              // collection frequency
    uint64_t docids_offset;   // offset in postings.docids.bin 
    uint64_t freqs_offset;    // offset in postings.freqs.bin
    uint32_t blocks;          // number of blocks
    
    TermMeta() : df(0), cf(0), docids_offset(0), freqs_offset(0), blocks(0) {}
};

// Lexicon: term -> TermMeta
class Lexicon {
private:
    std::unordered_map<std::string, TermMeta> terms;
    
public:
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Cannot open lexicon: " << path << std::endl;
            return false;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            std::istringstream iss(line);
            std::string term;
            TermMeta meta;
            
            if (iss >> term >> meta.df >> meta.cf >> meta.docids_offset >> meta.freqs_offset >> meta.blocks) {
                terms[term] = meta;
            }
        }
        
        file.close();
        std::cout << "Loaded " << terms.size() << " terms from lexicon" << std::endl;
        return true;
    }
    
    bool find(const std::string& term, TermMeta& out) const {
        auto it = terms.find(term);
        if (it != terms.end()) {
            out = it->second;
            return true;
        }
        return false;
    }
    
    size_t size() const { return terms.size(); }
};

// Collection statistics
class Stats {
public:
    uint64_t doc_count;
    double avgdl;
    
    Stats() : doc_count(0), avgdl(0.0) {}
    
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Cannot open stats: " << path << std::endl;
            return false;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            std::istringstream iss(line);
            std::string key;
            iss >> key;
            
            if (key == "doc_count") {
                iss >> doc_count;
            } else if (key == "avgdl") {
                iss >> avgdl;
            }
        }
        
        file.close();
        std::cout << "Loaded stats: doc_count=" << doc_count << ", avgdl=" << avgdl << std::endl;
        return doc_count > 0;
    }
};

// Document table: docID -> (originalDocID)
class DocTable {
private:
    std::vector<std::string> originalIDs;
    
public:
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Cannot open doc table: " << path << std::endl;
            return false;
        }
        
        std::string line;
        uint32_t maxDocID = 0;
        
        // First pass: determine the maximum internal docID
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            size_t tab = line.find('\t');
            if (tab != std::string::npos) {
                uint32_t docID = std::stoul(line.substr(0, tab));
                if (docID > maxDocID) maxDocID = docID;
            }
        }
        
        originalIDs.resize(maxDocID + 1);
        
        // Rewind and read again
        file.clear();
        file.seekg(0);
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            // Parse three-column format: internalDocID \t originalDocID
            size_t tab1 = line.find('\t');
            if (tab1 == std::string::npos) continue;
            
            uint32_t docID = std::stoul(line.substr(0, tab1));

            std::string originalID = line.substr(tab1 + 1);
            
            originalIDs[docID] = originalID;
        }
        
        file.close();
        std::cout << "Loaded " << originalIDs.size() << " documents from doc table" << std::endl;
        return true;
    }

    // Return the original document ID
    const std::string& originalID(uint32_t docID) const {
        static const std::string empty;
        if (docID < originalIDs.size()) {
            return originalIDs[docID];
        }
        return empty;
    }
    
    size_t size() const { return originalIDs.size(); }
};

// Document lengths: docID -> length
class DocLen {
private:
    std::vector<uint32_t> lengths;
    
public:
    bool load(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Cannot open doc lengths: " << path << std::endl;
            return false;
        }
        
        // Read all document lengths
        uint32_t len;
        while (file.read(reinterpret_cast<char*>(&len), sizeof(uint32_t))) {
            lengths.push_back(len);
        }
        
        file.close();
        std::cout << "Loaded lengths for " << lengths.size() << " documents" << std::endl;
        return !lengths.empty();
    }
    
    uint32_t len(uint32_t docID) const {
        if (docID < lengths.size()) {
            return lengths[docID];
        }
        return 0;
    }
    
    size_t size() const { return lengths.size(); }
};

/**
 * @brief Represents a posting list for a specific term.
 * 
 * Provides sequential access to compressed posting data (docIDs and term frequencies).
 * Implements local decoding rather than decompressing entire lists at once.
*/
class PostingList {
private:
    std::ifstream docidsFile;
    std::ifstream freqsFile;
    
    // block state
    uint32_t totalBlocks;
    uint32_t currentBlock;
    uint32_t blockLen;
    uint32_t blockPos;
    
    // current state
    uint32_t currentDocID;
    uint32_t currentFreq;
    bool hasMore;
    
    // buffer for current block
    std::vector<uint32_t> docIDsBuffer;
    std::vector<uint32_t> freqsBuffer;
    
    // load next block
    bool loadNextBlock() {
        if (currentBlock >= totalBlocks) {
            hasMore = false;
            return false;
        }
        
        // load docIDs block
        blockLen = varbyte::decode(docidsFile);
        if (!docidsFile.good()) {
            hasMore = false;
            return false;
        }

        docIDsBuffer.clear();
        docIDsBuffer.reserve(blockLen);
        
        uint32_t prevDocID = 0;
        for (uint32_t i = 0; i < blockLen; i++) {
            uint32_t gap = varbyte::decode(docidsFile);
            uint32_t docID = (i == 0) ? gap : (prevDocID + gap);
            docIDsBuffer.push_back(docID);
            prevDocID = docID;
        }
        
        // load freqs block
        uint32_t blockLenFreq = varbyte::decode(freqsFile);
        if (blockLenFreq != blockLen) {
            std::cerr << "Error: block length mismatch!" << std::endl;
            hasMore = false;
            return false;
        }
        
        freqsBuffer.clear();
        freqsBuffer.reserve(blockLen);
        for (uint32_t i = 0; i < blockLen; i++) {
            freqsBuffer.push_back(varbyte::decode(freqsFile));
        }
        
        blockPos = 0;
        currentBlock++;
        return true;
    }
    
public:
    PostingList() 
        : totalBlocks(0), currentBlock(0), blockLen(0), blockPos(0),
          currentDocID(0), currentFreq(0), hasMore(false) {}
    
    // open posting list for a term
    bool open(const TermMeta& meta, const std::string& indexDir) {
        std::string docPath = indexDir + "/postings.docids.bin";
        std::string freqPath = indexDir + "/postings.freqs.bin";

        docidsFile.open(docPath, std::ios::binary);
        freqsFile.open(freqPath, std::ios::binary);
        if (!docidsFile.is_open() || !freqsFile.is_open()) {
            std::cerr << "Failed to open posting list files\n";
            return false;
        }

        totalBlocks = meta.blocks;
        currentBlock = 0;
        
        // seek to offsets
        docidsFile.seekg(meta.docids_offset);
        freqsFile.seekg(meta.freqs_offset);
        
        hasMore = true;
        
        // load first block
        if (!loadNextBlock()) {
            return false;
        }
        
        // initialize current docID and freq
        if (blockLen > 0) {
            currentDocID = docIDsBuffer[0];
            currentFreq = freqsBuffer[0];
            return true;
        }
        
        hasMore = false;
        return false;
    }
    
    // move to next document
    bool next() {
        if (!hasMore) return false;
        
        blockPos++;
        
        // if still within current block
        if (blockPos < blockLen) {
            currentDocID = docIDsBuffer[blockPos];
            currentFreq = freqsBuffer[blockPos];
            return true;
        }
        
        // load next block
        if (loadNextBlock() && blockLen > 0) {
            currentDocID = docIDsBuffer[0];
            currentFreq = freqsBuffer[0];
            return true;
        }
        
        hasMore = false;
        return false;
    }
    
    // move to first docID >= target
    bool nextGEQ(uint32_t target) {
        while (hasMore && currentDocID < target) {
            if (!next()) {
                return false;
            }
        }
        return hasMore && currentDocID >= target;
    }
    
    // current docID
    uint32_t doc() const { return currentDocID; }
    
    // current freq
    uint32_t freq() const { return currentFreq; }
    
    // whether there are more documents
    bool valid() const { return hasMore; }
};

// Document content reader (based on offsets)
class DocContentFile {
private:
    struct DocOffset {
        uint64_t offset;
        uint32_t length;
    };
    
    std::string contentFilePath;
    std::vector<DocOffset> offsets;
    mutable std::ifstream contentFile;
    
public:
    DocContentFile() {}
    
    ~DocContentFile() {
        if (contentFile.is_open()) {
            contentFile.close();
        }
    }
    
    bool load(const std::string& offsetPath, const std::string& contentPath) {
        contentFilePath = contentPath;
        
        // 1. Load the offset table into memory
        std::ifstream offsetFile(offsetPath, std::ios::binary);
        if (!offsetFile.is_open()) {
            std::cerr << "Cannot open offset file: " << offsetPath << std::endl;
            return false;
        }
        
        // Read all offsets (each entry is 12 bytes: 8 bytes offset + 4 bytes length)
        uint64_t offset;
        uint32_t length;
        while (offsetFile.read(reinterpret_cast<char*>(&offset), sizeof(uint64_t))) {
            if (offsetFile.read(reinterpret_cast<char*>(&length), sizeof(uint32_t))) {
                offsets.push_back({offset, length});
            } else {
                break;
            }
        }
        offsetFile.close();
        
        std::cout << "Loaded offsets for " << offsets.size() << " documents" << std::endl;

        // Open the content file and keep it open for reuse
        contentFile.open(contentPath, std::ios::binary);
        if (!contentFile.is_open()) {
            std::cerr << "Cannot open content file: " << contentPath << std::endl;
            return false;
        }
        
        return true;
    }
    
    // Get single document content
    std::string get(uint32_t docID) const {
        if (docID >= offsets.size() || !contentFile.is_open()) {
            return "";
        }
        
        const DocOffset& doc = offsets[docID];
        
        // Seek to document position
        contentFile.seekg(doc.offset);
        
        // Read specified length
        std::string content(doc.length, '\0');
        contentFile.read(&content[0], doc.length);
        
        return content;
    }
    
    // Batch get (optimization: sort by offset and read sequentially)
    std::unordered_map<uint32_t, std::string> getBatch(const std::vector<uint32_t>& docIDs) const {
        std::unordered_map<uint32_t, std::string> results;
        
        if (docIDs.empty() || !contentFile.is_open()) {
            return results;
        }
        
        // sort by offset (to minimize seeks)
        std::vector<std::pair<uint32_t, DocOffset>> sorted;
        sorted.reserve(docIDs.size());
        
        for (uint32_t docID : docIDs) {
            if (docID < offsets.size()) {
                sorted.push_back({docID, offsets[docID]});
            }
        }
        
        std::sort(sorted.begin(), sorted.end(), 
                 [](const auto& a, const auto& b) {
                     return a.second.offset < b.second.offset;
                 });
        
        // read documents in sorted order
        for (const auto& [docID, doc] : sorted) {
            contentFile.seekg(doc.offset);
            std::string content(doc.length, '\0');
            contentFile.read(&content[0], doc.length);
            results[docID] = content;
        }
        
        return results;
    }
    
    size_t size() const { return offsets.size(); }
};


#endif // INDEX_READER_HPP

