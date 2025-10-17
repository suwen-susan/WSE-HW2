#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include "utils.hpp"

namespace fs = std::filesystem;

// 辅助函数：截取文本片段作为摘要
// 处理特殊字符（tab, 换行符）避免破坏 TSV 格式
inline std::string makeSnippet(const std::string& text, size_t maxLen = 60) {
    std::string snippet;
    snippet.reserve(maxLen);
    
    size_t count = 0;
    for (unsigned char ch : text) {
        if (count >= maxLen) break;
        
        // 将 tab 和换行符替换为空格
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            if (!snippet.empty() && snippet.back() != ' ') {
                snippet.push_back(' ');
            }
        } else if (ch >= 32 || ch == '\t') {  // 可打印字符
            snippet.push_back(ch);
            count++;
        }
    }
    
    // 去除尾部空格
    while (!snippet.empty() && snippet.back() == ' ') {
        snippet.pop_back();
    }
    
    return snippet;
}

class IndexBuilder {
private:
    uint32_t currentDocID;
    std::string outputDir;
    int batchNumber;
    
    // 文档表文件
    std::ofstream docTableFile;
    std::ofstream docContentFile;
    std::ofstream docOffsetFile;     // docID -> (offset, length)
    
    // 扁平 posting 输出文件（分片滚动写）
    std::ofstream postingsOut;
    size_t partByteLimit;      // 每个分片的字节阈值
    size_t bytesWrittenInPart; // 当前分片累计字节（粗略估算）
    size_t linesWrittenInPart; // 当前分片累计行数
    
    // 打开新的 posting 分片文件
    void openNewPart() {
        if (postingsOut.is_open()) {
            postingsOut.close();
        }
        std::string filename = outputDir + "/postings_part_" + std::to_string(batchNumber) + ".tsv";
        postingsOut.open(filename, std::ios::out | std::ios::binary);
        if (!postingsOut.is_open()) {
            std::cerr << "Failed to open " << filename << std::endl;
            exit(1);
        }
        bytesWrittenInPart = 0;
        linesWrittenInPart = 0;
        std::cout << "Opened " << filename << std::endl;
    }
    
    // 检查并滚动到新分片
    void rolloverIfNeeded() {
        if (bytesWrittenInPart >= partByteLimit) {
            std::cout << "Batch " << batchNumber << " written: " 
                      << linesWrittenInPart << " postings (~" 
                      << (bytesWrittenInPart / (1024*1024)) << " MB)" << std::endl;
            batchNumber++;
            openNewPart();
        }
    }
    
public:
    IndexBuilder(const std::string& outDir, size_t partBytes = (size_t)2ULL * 1024 * 1024 * 1024) 
        : currentDocID(0), outputDir(outDir), batchNumber(0),
          partByteLimit(partBytes), bytesWrittenInPart(0), linesWrittenInPart(0) {
        
        // 创建输出目录
        fs::create_directories(outputDir);
        
        // 打开文档表文件
        docTableFile.open(outputDir + "/doc_table.txt", std::ios::out);
        
        // 文档内容：追加所有文档内容
        docContentFile.open(outputDir + "/doc_content.bin", 
                           std::ios::out | std::ios::binary);
        
        // 偏移量表：docID -> (offset, length)
        docOffsetFile.open(outputDir + "/doc_offset.bin", 
                          std::ios::out | std::ios::binary);
        
        if (!docTableFile.is_open() || !docContentFile.is_open() || 
            !docOffsetFile.is_open()) {
            std::cerr << "Failed to open doc_table.txt" << std::endl;
            exit(1);
        }
        
        // 打开第一个 posting 分片
        openNewPart();
    }
    
    ~IndexBuilder() {
        if (docTableFile.is_open()) {
            docTableFile.close();
        }
        if (docContentFile.is_open()) docContentFile.close();
        if (docOffsetFile.is_open()) docOffsetFile.close();
        if (postingsOut.is_open()) {
            postingsOut.close();
        }
    }
   
    // 解析单个文档
    void parseDocument(const std::string& docName, const std::string& content) {
        // // 记录文档ID和文档名的映射
        // // 格式：internalDocID \t originalDocID \t passage_snippet
        // std::string snippet = makeSnippet(content, 60);
        // docTableFile << currentDocID << "\t" << docName << "\t" << snippet << "\n";
        
         // 1. 文档表：docID -> originalID
        docTableFile << currentDocID << "\t" << docName << "\n";
        
        // 2. 写入文档内容并记录偏移量
        uint64_t offset = docContentFile.tellp();  // 当前写位置
        
        // 清理内容（去除 tab 和换行符，保持单行便于处理）
        std::string cleanContent = content;
        std::replace(cleanContent.begin(), cleanContent.end(), '\t', ' ');
        std::replace(cleanContent.begin(), cleanContent.end(), '\n', ' ');
        std::replace(cleanContent.begin(), cleanContent.end(), '\r', ' ');
        
        // 写入内容（带换行符作为分隔）
        docContentFile << cleanContent << "\n";
        
        uint64_t afterOffset = docContentFile.tellp();
        uint32_t length = static_cast<uint32_t>(afterOffset - offset - 1);  // 减去换行符
        
        // 3. 写入偏移量信息：offset (8 bytes) + length (4 bytes)
        docOffsetFile.write(reinterpret_cast<const char*>(&offset), sizeof(uint64_t));
        docOffsetFile.write(reinterpret_cast<const char*>(&length), sizeof(uint32_t));
        
        
        // 统计当前文档中每个term的频率
        std::unordered_map<std::string, uint32_t> termFreq;
        termFreq.reserve(256); // 预分配减少rehash
        
        // 使用新的分词函数（保留所有词，包括数字、单字符、停用词）
        for (const auto& token : tokenize_words(content)) {
            termFreq[token]++;
        }
        
        // 逐 posting 行写出：term<TAB>docID<TAB>tf
        for (const auto& kv : termFreq) {
            const std::string& term = kv.first;
            uint32_t tf = kv.second;
            
            postingsOut << term << "\t" << currentDocID << "\t" << tf << "\n";
            
            // 粗略估算字节数（避免频繁调用 tellp 的系统开销）
            // term + tab + docID(最多10位) + tab + tf(最多5位) + newline
            bytesWrittenInPart += term.size() + 1 + 10 + 1 + 5 + 1;
            linesWrittenInPart++;
        }
        
        currentDocID++;
        
        // 检查是否需要滚动到新分片
        rolloverIfNeeded();
    }
    
    // 处理完所有文档后的收尾工作
    void finalize() {
        // 关闭所有文件
        if (postingsOut.is_open()) {
            std::cout << "Batch " << batchNumber << " written: " 
                      << linesWrittenInPart << " postings (~" 
                      << (bytesWrittenInPart / (1024*1024)) << " MB)" << std::endl;
            postingsOut.close();
        }
        if (docTableFile.is_open()) {
            docTableFile.close();
        }
        
        std::cout << "\nIndexing complete!" << std::endl;
        std::cout << "Total documents processed: " << currentDocID << std::endl;
        std::cout << "Total intermediate files: " << (batchNumber + 1) << std::endl;
        std::cout << "\nNext step: Use msort to globally sort posting files:" << std::endl;
        std::cout << "  Example: msort -t '\\t' -k 1,1 -k 2,2n postings_part_*.tsv > postings_sorted.tsv" << std::endl;
    }
    
    // 处理MS MARCO数据集格式：tsv文件 (docID \t passage)
    void processMSMARCO(const std::string& inputFile) {
        std::ifstream inFile(inputFile);
        if (!inFile.is_open()) {
            std::cerr << "Cannot open input file: " << inputFile << std::endl;
            return;
        }
        
        std::string line;
        int lineCount = 0;
        
        while (std::getline(inFile, line)) {
            lineCount++;
            
            if (lineCount % 10000 == 0) {
                std::cout << "Processed " << lineCount << " documents..." << std::endl;
            }
            
            // 解析TSV格式：docID \t passage
            size_t tabPos = line.find('\t');
            if (tabPos == std::string::npos) continue;
            
            std::string docName = line.substr(0, tabPos);
            std::string content = line.substr(tabPos + 1);
            
            parseDocument(docName, content);
        }
        
        inFile.close();
        finalize();
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input_file> <output_dir> [part_size_gb]" << std::endl;
        std::cout << "Example: " << argv[0] << " collection.tsv ./index_output" << std::endl;
        std::cout << "         " << argv[0] << " collection.tsv ./index_output 4" << std::endl;
        std::cout << "  part_size_gb: Size of each intermediate file in GB (default: 2)" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputDir = argv[2];
    
    // 可选：从命令行指定分片大小（GB）
    size_t partSizeGB = 2;
    if (argc >= 4) {
        partSizeGB = std::stoull(argv[3]);
    }
    size_t partBytes = partSizeGB * 1024ULL * 1024ULL * 1024ULL;
    
    std::cout << "Building inverted index (Phase 1: Indexing)..." << std::endl;
    std::cout << "Input: " << inputFile << std::endl;
    std::cout << "Output: " << outputDir << std::endl;
    std::cout << "Part size: " << partSizeGB << " GB" << std::endl;
    
    // 创建索引构建器（分片大小可配置，默认2GB）
    IndexBuilder builder(outputDir, partBytes);
    
    // 处理数据集
    builder.processMSMARCO(inputFile);
    
    std::cout << "\nIndex building phase 1 complete!" << std::endl;
    
    return 0;
}