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


class IndexBuilder {
private:
    std::unordered_map<std::string, std::vector<Posting>> indexMap;
    uint32_t currentDocID;
    std::string outputDir;
    size_t memoryLimit; // 内存限制（posting数量）
    int batchNumber;
    
    // 文档ID到文档名的映射
    std::ofstream docTableFile;
    
public:
    IndexBuilder(const std::string& outDir, size_t memLimit = 1000000) 
        : currentDocID(0), outputDir(outDir), memoryLimit(memLimit), batchNumber(0) {
        
        // 创建输出目录
        fs::create_directories(outputDir);
        
        // 打开文档表文件
        docTableFile.open(outputDir + "/doc_table.txt");
    }
    
    ~IndexBuilder() {
        if (docTableFile.is_open()) {
            docTableFile.close();
        }
    }
   
    // 解析单个文档
    void parseDocument(const std::string& docName, const std::string& content) {
        // 记录文档ID和文档名的映射
        docTableFile << currentDocID << "\t" << docName << "\n";
        
        // 统计当前文档中每个term的频率
        std::unordered_map<std::string, uint32_t> termFreq;

        std::string normalizedContent = normalize(content);
        
        std::istringstream iss(normalizedContent);
        std::string token;
        
        while (iss >> token) {
            if (!token.empty() && token.length() > 1) { // 过滤单字符词
                termFreq[token]++;
            }
        }
        
        // 将term frequencies添加到索引中
        for (const auto& [term, freq] : termFreq) {
            indexMap[term].emplace_back(currentDocID, freq);
        }
        
        currentDocID++;
        
        // 检查内存使用，如果超过限制则写出到磁盘
        size_t totalPostings = 0;
        for (const auto& [term, postings] : indexMap) {
            totalPostings += postings.size();
        }
        
        if (totalPostings >= memoryLimit) {
            flushToDisk();
        }
    }
    
    // 将内存中的索引写入中间文件
    void flushToDisk() {
        if (indexMap.empty()) return;
        
        std::cout << "Flushing batch " << batchNumber << " to disk..." << std::endl;
        
        // 准备排序的term-postings列表
        std::vector<TermPostings> sortedIndex;
        sortedIndex.reserve(indexMap.size());
        
        for (auto& [term, postings] : indexMap) {
            // 对每个term的postings按docID排序
            std::sort(postings.begin(), postings.end());
            
            TermPostings tp(term);
            tp.postings = std::move(postings);
            sortedIndex.push_back(std::move(tp));
        }
        
        // 按term字典序排序
        std::sort(sortedIndex.begin(), sortedIndex.end(), 
                  [](const TermPostings& a, const TermPostings& b) {
                      return a.term < b.term;
                  });
        
        // 写入中间文件（文本格式，便于调试）
        std::string filename = outputDir + "/intermediate_" + 
                               std::to_string(batchNumber) + ".txt";
        std::ofstream outFile(filename);
        
        for (const auto& tp : sortedIndex) {
            size_t df = tp.postings.size();
            outFile << tp.term << " " << df;;
            for (const auto& posting : tp.postings) {
                outFile << " " << posting.docID << ":" << posting.frequency;
            }
            outFile << "\n";
        }
        
        outFile.close();
        
        std::cout << "Batch " << batchNumber << " written: " 
                  << sortedIndex.size() << " terms" << std::endl;
        
        batchNumber++;
        indexMap.clear();
    }
    
    // 处理完所有文档后的收尾工作
    void finalize() {
        // 写出剩余的索引数据
        if (!indexMap.empty()) {
            flushToDisk();
        }
        
        std::cout << "\nIndexing complete!" << std::endl;
        std::cout << "Total documents processed: " << currentDocID << std::endl;
        std::cout << "Total intermediate files: " << batchNumber << std::endl;
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
        std::cout << "Usage: " << argv[0] << " <input_file> <output_dir>" << std::endl;
        std::cout << "Example: " << argv[0] << " collection.tsv ./index_output" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputDir = argv[2];
    
    std::cout << "Building inverted index..." << std::endl;
    std::cout << "Input: " << inputFile << std::endl;
    std::cout << "Output: " << outputDir << std::endl;
    
    // 创建索引构建器（内存限制：100万个postings）
    IndexBuilder builder(outputDir, 1000000);
    
    // 处理数据集
    builder.processMSMARCO(inputFile);
    
    std::cout << "\nIndex building phase 1 complete!" << std::endl;
    
    return 0;
}