#ifndef INDEX_READER_HPP
#define INDEX_READER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <iostream>
#include "varbyte.hpp"

// 词项元数据
struct TermMeta {
    uint32_t df;              // document frequency
    uint64_t cf;              // collection frequency
    uint64_t docids_offset;   // 在 postings.docids.bin 中的偏移
    uint64_t freqs_offset;    // 在 postings.freqs.bin 中的偏移
    uint32_t blocks;          // 块数量
    
    TermMeta() : df(0), cf(0), docids_offset(0), freqs_offset(0), blocks(0) {}
};

// 词典：term -> TermMeta
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

// 统计信息
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

// 文档表：docID -> (originalDocID, snippet)
class DocTable {
private:
    std::vector<std::string> originalIDs;
    std::vector<std::string> snippets;
    
public:
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Cannot open doc table: " << path << std::endl;
            return false;
        }
        
        std::string line;
        uint32_t maxDocID = 0;
        
        // 先统计最大 docID
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            size_t tab = line.find('\t');
            if (tab != std::string::npos) {
                uint32_t docID = std::stoul(line.substr(0, tab));
                if (docID > maxDocID) maxDocID = docID;
            }
        }
        
        originalIDs.resize(maxDocID + 1);
        snippets.resize(maxDocID + 1);
        
        // 重新读取
        file.clear();
        file.seekg(0);
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            // 解析三列格式：internalDocID \t originalDocID \t snippet
            size_t tab1 = line.find('\t');
            if (tab1 == std::string::npos) continue;
            
            uint32_t docID = std::stoul(line.substr(0, tab1));
            
            size_t tab2 = line.find('\t', tab1 + 1);
            if (tab2 == std::string::npos) continue;
            
            std::string originalID = line.substr(tab1 + 1, tab2 - tab1 - 1);
            std::string snippet = line.substr(tab2 + 1);
            
            originalIDs[docID] = originalID;
            snippets[docID] = snippet;
        }
        
        file.close();
        std::cout << "Loaded " << snippets.size() << " documents from doc table" << std::endl;
        return true;
    }
    
    // 返回文档片段（用于显示）
    const std::string& name(uint32_t docID) const {
        static const std::string empty;
        if (docID < snippets.size()) {
            return snippets[docID];
        }
        return empty;
    }
    
    // 返回原始文档 ID
    const std::string& originalID(uint32_t docID) const {
        static const std::string empty;
        if (docID < originalIDs.size()) {
            return originalIDs[docID];
        }
        return empty;
    }
    
    size_t size() const { return snippets.size(); }
};

// 文档长度：docID -> length
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
        
        // 读取所有文档长度
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

// 倒排表遍历器（支持 DAAT）
class PostingList {
private:
    std::ifstream* docidsFile;
    std::ifstream* freqsFile;
    
    // 块状态
    uint32_t totalBlocks;
    uint32_t currentBlock;
    uint32_t blockLen;
    uint32_t blockPos;
    
    // 当前文档状态
    uint32_t currentDocID;
    uint32_t currentFreq;
    bool hasMore;
    
    // 块缓冲
    std::vector<uint32_t> docIDsBuffer;
    std::vector<uint32_t> freqsBuffer;
    
    // 读取下一块
    bool loadNextBlock() {
        if (currentBlock >= totalBlocks) {
            hasMore = false;
            return false;
        }
        
        // 读取 docIDs 块
        blockLen = varbyte::decode(*docidsFile);
        docIDsBuffer.clear();
        docIDsBuffer.reserve(blockLen);
        
        uint32_t prevDocID = 0;
        for (uint32_t i = 0; i < blockLen; i++) {
            uint32_t gap = varbyte::decode(*docidsFile);
            uint32_t docID = (i == 0) ? gap : (prevDocID + gap);
            docIDsBuffer.push_back(docID);
            prevDocID = docID;
        }
        
        // 读取 frequencies 块
        uint32_t blockLenFreq = varbyte::decode(*freqsFile);
        if (blockLenFreq != blockLen) {
            std::cerr << "Error: block length mismatch!" << std::endl;
            hasMore = false;
            return false;
        }
        
        freqsBuffer.clear();
        freqsBuffer.reserve(blockLen);
        for (uint32_t i = 0; i < blockLen; i++) {
            freqsBuffer.push_back(varbyte::decode(*freqsFile));
        }
        
        blockPos = 0;
        currentBlock++;
        return true;
    }
    
public:
    PostingList() 
        : docidsFile(nullptr), freqsFile(nullptr),
          totalBlocks(0), currentBlock(0), blockLen(0), blockPos(0),
          currentDocID(0), currentFreq(0), hasMore(false) {}
    
    // 打开倒排表
    bool open(const TermMeta& meta, std::ifstream& docids, std::ifstream& freqs) {
        docidsFile = &docids;
        freqsFile = &freqs;
        totalBlocks = meta.blocks;
        currentBlock = 0;
        
        // seek 到起始位置
        docidsFile->seekg(meta.docids_offset);
        freqsFile->seekg(meta.freqs_offset);
        
        hasMore = true;
        
        // 加载第一块
        if (!loadNextBlock()) {
            return false;
        }
        
        // 设置第一个文档
        if (blockLen > 0) {
            currentDocID = docIDsBuffer[0];
            currentFreq = freqsBuffer[0];
            return true;
        }
        
        hasMore = false;
        return false;
    }
    
    // 移动到下一个文档
    bool next() {
        if (!hasMore) return false;
        
        blockPos++;
        
        // 如果当前块还有数据
        if (blockPos < blockLen) {
            currentDocID = docIDsBuffer[blockPos];
            currentFreq = freqsBuffer[blockPos];
            return true;
        }
        
        // 需要加载下一块
        if (loadNextBlock() && blockLen > 0) {
            currentDocID = docIDsBuffer[0];
            currentFreq = freqsBuffer[0];
            return true;
        }
        
        hasMore = false;
        return false;
    }
    
    // 移动到 >= target 的第一个文档
    bool nextGEQ(uint32_t target) {
        while (hasMore && currentDocID < target) {
            if (!next()) {
                return false;
            }
        }
        return hasMore && currentDocID >= target;
    }
    
    // 当前文档 ID
    uint32_t doc() const { return currentDocID; }
    
    // 当前频率
    uint32_t freq() const { return currentFreq; }
    
    // 是否还有更多文档
    bool valid() const { return hasMore; }
};

#endif // INDEX_READER_HPP

