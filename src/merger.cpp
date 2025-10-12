#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <cstdint>
#include "varbyte.hpp"

namespace fs = std::filesystem;

// 倒排索引合并器：将排序后的 posting 流聚合为块化压缩索引
class IndexMerger {
private:
    // 配置参数
    static constexpr size_t BLOCK_SIZE = 128;        // 每块的 posting 数量
    static constexpr size_t READ_BUFFER_SIZE = 8 * 1024 * 1024;  // 8MB 读缓冲
    
    // 输入输出路径
    std::string inputFile;      // 排序后的 postings_sorted.tsv
    std::string outputDir;      // 输出目录
    
    // 输出文件流
    std::ofstream docIdsFile;   // docIDs 块化压缩文件
    std::ofstream freqsFile;    // frequencies 块化压缩文件
    std::ofstream lexiconFile;  // 词典文件（文本格式，便于调试）
    std::ofstream statsFile;    // 统计信息文件
    
    // 统计信息
    uint64_t totalTerms;        // 词项总数
    uint64_t totalPostings;     // posting 总数
    uint64_t docCount;          // 文档总数
    std::vector<uint32_t> docLengths; // 每个文档的长度（用于计算 avgdl）
    
    // 单个 posting 结构
    struct Posting {
        uint32_t docID;
        uint32_t frequency;
        
        Posting(uint32_t d, uint32_t f) : docID(d), frequency(f) {}
    };
    
public:
    IndexMerger(const std::string& input, const std::string& outDir)
        : inputFile(input), outputDir(outDir),
          totalTerms(0), totalPostings(0), docCount(0) {
        
        // 创建输出目录
        fs::create_directories(outputDir);
        
        // 打开输出文件
        docIdsFile.open(outputDir + "/postings.docids.bin", 
                        std::ios::out | std::ios::binary);
        freqsFile.open(outputDir + "/postings.freqs.bin", 
                       std::ios::out | std::ios::binary);
        lexiconFile.open(outputDir + "/lexicon.tsv", 
                         std::ios::out);
        
        if (!docIdsFile.is_open() || !freqsFile.is_open() || !lexiconFile.is_open()) {
            std::cerr << "Failed to open output files" << std::endl;
            exit(1);
        }
        
        // 写词典文件头（便于理解格式）
        lexiconFile << "# term\tdf\tcf\tdocids_offset\tfreqs_offset\tblocks_count\n";
    }
    
    ~IndexMerger() {
        if (docIdsFile.is_open()) docIdsFile.close();
        if (freqsFile.is_open()) freqsFile.close();
        if (lexiconFile.is_open()) lexiconFile.close();
        if (statsFile.is_open()) statsFile.close();
    }
    
    // 主处理流程
    void process() {
        std::ifstream inFile(inputFile);
        if (!inFile.is_open()) {
            std::cerr << "Cannot open input file: " << inputFile << std::endl;
            exit(1);
        }
        
        // 设置读缓冲以提升 I/O 效率
        std::vector<char> readBuffer(READ_BUFFER_SIZE);
        inFile.rdbuf()->pubsetbuf(readBuffer.data(), READ_BUFFER_SIZE);
        
        std::cout << "Merging sorted postings into compressed index..." << std::endl;
        std::cout << "Input: " << inputFile << std::endl;
        std::cout << "Output: " << outputDir << std::endl;
        std::cout << "Block size: " << BLOCK_SIZE << std::endl;
        
        // 流式处理：逐行读取，按 term 分组
        std::string line;
        std::string currentTerm;
        std::vector<Posting> currentPostings;
        currentPostings.reserve(1024); // 预分配空间
        
        uint64_t linesProcessed = 0;
        
        while (std::getline(inFile, line)) {
            if (line.empty() || line[0] == '#') continue; // 跳过空行和注释
            
            // 解析行：term<TAB>docID<TAB>tf
            size_t tab1 = line.find('\t');
            size_t tab2 = line.find('\t', tab1 + 1);
            
            if (tab1 == std::string::npos || tab2 == std::string::npos) {
                std::cerr << "Warning: malformed line: " << line << std::endl;
                continue;
            }
            
            std::string term = line.substr(0, tab1);
            uint32_t docID = std::stoul(line.substr(tab1 + 1, tab2 - tab1 - 1));
            uint32_t tf = std::stoul(line.substr(tab2 + 1));
            
            // 更新文档计数
            if (docID >= docCount) {
                docCount = docID + 1;
            }
            
            // 检查是否切换到新 term
            if (term != currentTerm) {
                if (!currentPostings.empty()) {
                    // 写出前一个 term 的倒排表
                    writeInvertedList(currentTerm, currentPostings);
                    currentPostings.clear();
                }
                currentTerm = term;
            }
            
            // 累积当前 term 的 posting
            currentPostings.emplace_back(docID, tf);
            
            linesProcessed++;
            if (linesProcessed % 10000000 == 0) {
                std::cout << "Processed " << (linesProcessed / 1000000) 
                          << "M postings, " << totalTerms << " terms..." << std::endl;
            }
        }
        
        // 写出最后一个 term
        if (!currentPostings.empty()) {
            writeInvertedList(currentTerm, currentPostings);
        }
        
        inFile.close();
        
        // 写统计信息
        writeStats();
        
        std::cout << "\nMerging complete!" << std::endl;
        std::cout << "Total terms: " << totalTerms << std::endl;
        std::cout << "Total postings: " << totalPostings << std::endl;
        std::cout << "Total documents: " << docCount << std::endl;
    }
    
private:
    // 写出单个 term 的倒排表（块化压缩）
    void writeInvertedList(const std::string& term, const std::vector<Posting>& postings) {
        if (postings.empty()) return;
        
        // 记录写入前的偏移量
        uint64_t docIdsOffset = docIdsFile.tellp();
        uint64_t freqsOffset = freqsFile.tellp();
        
        // 计算统计信息
        uint32_t df = static_cast<uint32_t>(postings.size());  // document frequency
        uint64_t cf = 0;  // collection frequency (sum of all tf)
        
        // 按块写入
        size_t blocksCount = 0;
        for (size_t i = 0; i < postings.size(); i += BLOCK_SIZE) {
            size_t blockLen = std::min(BLOCK_SIZE, postings.size() - i);
            
            // 写 docIDs 块
            writeDocIDsBlock(postings, i, blockLen);
            
            // 写 frequencies 块
            writeFrequenciesBlock(postings, i, blockLen, cf);
            
            blocksCount++;
        }
        
        // 写词典条目
        lexiconFile << term << "\t" 
                    << df << "\t" 
                    << cf << "\t"
                    << docIdsOffset << "\t" 
                    << freqsOffset << "\t"
                    << blocksCount << "\n";
        
        totalTerms++;
        totalPostings += df;
    }
    
    // 写 docIDs 块（差分 + VarByte 编码）
    void writeDocIDsBlock(const std::vector<Posting>& postings, 
                          size_t start, size_t length) {
        // 块格式：block_length + docID序列（差分编码）
        
        // 写块长度
        varbyte::encode(docIdsFile, static_cast<uint32_t>(length));
        
        // 写 docID 序列（差分编码）
        uint32_t prevDocID = 0;
        for (size_t i = 0; i < length; i++) {
            uint32_t docID = postings[start + i].docID;
            uint32_t gap = (i == 0) ? docID : (docID - prevDocID);
            varbyte::encode(docIdsFile, gap);
            prevDocID = docID;
        }
    }
    
    // 写 frequencies 块（VarByte 编码）
    void writeFrequenciesBlock(const std::vector<Posting>& postings, 
                               size_t start, size_t length, uint64_t& cf) {
        // 块格式：block_length + tf序列
        
        // 写块长度
        varbyte::encode(freqsFile, static_cast<uint32_t>(length));
        
        // 写 tf 序列
        for (size_t i = 0; i < length; i++) {
            uint32_t tf = postings[start + i].frequency;
            varbyte::encode(freqsFile, tf);
            cf += tf;
            
            // 更新文档长度（用于 BM25 的 avgdl）
            uint32_t docID = postings[start + i].docID;
            if (docID >= docLengths.size()) {
                docLengths.resize(docID + 1, 0);
            }
            docLengths[docID] += tf;
        }
    }
    
    // 写统计信息和文档长度
    void writeStats() {
        // 写文档长度文件（BM25 需要）
        std::ofstream docLenFile(outputDir + "/doc_len.bin", std::ios::out | std::ios::binary);
        if (docLenFile.is_open()) {
            for (uint32_t len : docLengths) {
                docLenFile.write(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
            }
            docLenFile.close();
            std::cout << "Wrote document lengths for " << docLengths.size() << " documents" << std::endl;
        } else {
            std::cerr << "Warning: Failed to write doc_len.bin" << std::endl;
        }
        
        // 写统计信息
        statsFile.open(outputDir + "/stats.txt", std::ios::out);
        if (!statsFile.is_open()) {
            std::cerr << "Failed to open stats file" << std::endl;
            return;
        }
        
        // 计算平均文档长度
        uint64_t totalDocLength = 0;
        for (uint32_t len : docLengths) {
            totalDocLength += len;
        }
        double avgdl = (docCount > 0) ? static_cast<double>(totalDocLength) / docCount : 0.0;
        
        // 写统计信息
        statsFile << "# Index Statistics\n";
        statsFile << "doc_count\t" << docCount << "\n";
        statsFile << "total_terms\t" << totalTerms << "\n";
        statsFile << "total_postings\t" << totalPostings << "\n";
        statsFile << "avgdl\t" << avgdl << "\n";
        statsFile << "total_doc_length\t" << totalDocLength << "\n";
        
        statsFile.close();
        
        std::cout << "Average document length: " << avgdl << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <sorted_postings_file> <output_dir>" << std::endl;
        std::cout << "Example: " << argv[0] << " postings_sorted.tsv ./index" << std::endl;
        std::cout << "\nThis program merges sorted postings into a compressed inverted index." << std::endl;
        std::cout << "Input format: term<TAB>docID<TAB>tf (sorted by term, then by docID)" << std::endl;
        std::cout << "\nOutput files:" << std::endl;
        std::cout << "  - postings.docids.bin: Compressed docIDs (gap-encoded VarByte)" << std::endl;
        std::cout << "  - postings.freqs.bin: Compressed frequencies (VarByte)" << std::endl;
        std::cout << "  - lexicon.tsv: Term dictionary with offsets" << std::endl;
        std::cout << "  - stats.txt: Index statistics (doc_count, avgdl, etc.)" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputDir = argv[2];
    
    std::cout << "Inverted Index Merger (Phase 2)" << std::endl;
    std::cout << "===============================" << std::endl;
    
    IndexMerger merger(inputFile, outputDir);
    merger.process();
    
    std::cout << "\nIndex merging phase 2 complete!" << std::endl;
    
    return 0;
}

