#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include "varbyte.hpp"

// 索引检查工具：读取并显示倒排表内容，验证索引正确性
class IndexInspector {
private:
    std::string indexDir;
    
public:
    IndexInspector(const std::string& dir) : indexDir(dir) {}
    
    // 显示指定 term 的倒排表
    void inspectTerm(const std::string& term) {
        // 1. 从词典中查找 term
        std::ifstream lexicon(indexDir + "/lexicon.tsv");
        if (!lexicon.is_open()) {
            std::cerr << "Cannot open lexicon file" << std::endl;
            return;
        }
        
        std::string line;
        bool found = false;
        uint32_t df, cf, blocks;
        uint64_t docids_offset, freqs_offset;
        
        while (std::getline(lexicon, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            std::istringstream iss(line);
            std::string t;
            iss >> t >> df >> cf >> docids_offset >> freqs_offset >> blocks;
            
            if (t == term) {
                found = true;
                break;
            }
        }
        lexicon.close();
        
        if (!found) {
            std::cout << "Term '" << term << "' not found in lexicon." << std::endl;
            return;
        }
        
        std::cout << "\n=== Term: " << term << " ===" << std::endl;
        std::cout << "Document Frequency (df): " << df << std::endl;
        std::cout << "Collection Frequency (cf): " << cf << std::endl;
        std::cout << "Blocks: " << blocks << std::endl;
        std::cout << "DocIDs offset: " << docids_offset << std::endl;
        std::cout << "Freqs offset: " << freqs_offset << std::endl;
        
        // 2. 读取倒排表
        std::ifstream docidsFile(indexDir + "/postings.docids.bin", std::ios::binary);
        std::ifstream freqsFile(indexDir + "/postings.freqs.bin", std::ios::binary);
        
        if (!docidsFile.is_open() || !freqsFile.is_open()) {
            std::cerr << "Cannot open posting files" << std::endl;
            return;
        }
        
        docidsFile.seekg(docids_offset);
        freqsFile.seekg(freqs_offset);
        
        std::cout << "\nPostings List:" << std::endl;
        std::cout << std::setw(10) << "DocID" << std::setw(10) << "Freq" << std::endl;
        std::cout << std::string(20, '-') << std::endl;
        
        uint32_t totalPostings = 0;
        uint32_t totalFreq = 0;
        
        // 逐块读取
        for (uint32_t b = 0; b < blocks; b++) {
            uint32_t blockLen = varbyte::decode(docidsFile);
            uint32_t blockLenFreq = varbyte::decode(freqsFile);
            
            if (blockLen != blockLenFreq) {
                std::cerr << "Error: block length mismatch!" << std::endl;
                return;
            }
            
            // 读取 docIDs（差分解码）
            std::vector<uint32_t> docids;
            uint32_t prevDocID = 0;
            for (uint32_t i = 0; i < blockLen; i++) {
                uint32_t gap = varbyte::decode(docidsFile);
                uint32_t docID = (i == 0) ? gap : (prevDocID + gap);
                docids.push_back(docID);
                prevDocID = docID;
            }
            
            // 读取 frequencies
            std::vector<uint32_t> freqs;
            for (uint32_t i = 0; i < blockLen; i++) {
                uint32_t freq = varbyte::decode(freqsFile);
                freqs.push_back(freq);
            }
            
            // 显示
            for (uint32_t i = 0; i < blockLen; i++) {
                std::cout << std::setw(10) << docids[i] 
                          << std::setw(10) << freqs[i] << std::endl;
                totalPostings++;
                totalFreq += freqs[i];
            }
        }
        
        std::cout << std::string(20, '-') << std::endl;
        std::cout << "Total postings: " << totalPostings 
                  << " (expected: " << df << ")" << std::endl;
        std::cout << "Total frequency: " << totalFreq 
                  << " (expected: " << cf << ")" << std::endl;
        
        if (totalPostings != df || totalFreq != cf) {
            std::cout << "WARNING: Mismatch detected!" << std::endl;
        } else {
            std::cout << "✓ Verification passed!" << std::endl;
        }
        
        docidsFile.close();
        freqsFile.close();
    }
    
    // 显示索引统计信息
    void showStats() {
        std::ifstream stats(indexDir + "/stats.txt");
        if (!stats.is_open()) {
            std::cerr << "Cannot open stats file" << std::endl;
            return;
        }
        
        std::cout << "\n=== Index Statistics ===" << std::endl;
        std::string line;
        while (std::getline(stats, line)) {
            if (!line.empty() && line[0] != '#') {
                std::cout << line << std::endl;
            }
        }
        stats.close();
    }
    
    // 显示词典概览
    void showLexiconSummary(size_t topN = 20) {
        std::ifstream lexicon(indexDir + "/lexicon.tsv");
        if (!lexicon.is_open()) {
            std::cerr << "Cannot open lexicon file" << std::endl;
            return;
        }
        
        std::cout << "\n=== Lexicon Summary (top " << topN << " terms) ===" << std::endl;
        std::cout << std::setw(15) << "Term" 
                  << std::setw(8) << "DF" 
                  << std::setw(10) << "CF" << std::endl;
        std::cout << std::string(33, '-') << std::endl;
        
        std::string line;
        size_t count = 0;
        while (std::getline(lexicon, line) && count < topN + 1) {
            if (line.empty() || line[0] == '#') continue;
            
            std::istringstream iss(line);
            std::string term;
            uint32_t df, cf;
            iss >> term >> df >> cf;
            
            std::cout << std::setw(15) << term 
                      << std::setw(8) << df 
                      << std::setw(10) << cf << std::endl;
            count++;
        }
        
        lexicon.close();
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <index_dir> [term1] [term2] ..." << std::endl;
        std::cout << "\nExamples:" << std::endl;
        std::cout << "  " << argv[0] << " ./index               # Show stats and lexicon summary" << std::endl;
        std::cout << "  " << argv[0] << " ./index fox dog       # Inspect specific terms" << std::endl;
        std::cout << "\nThis tool inspects and verifies the compressed inverted index." << std::endl;
        return 1;
    }
    
    std::string indexDir = argv[1];
    IndexInspector inspector(indexDir);
    
    std::cout << "Index Inspector" << std::endl;
    std::cout << "===============" << std::endl;
    
    // 总是显示统计信息
    inspector.showStats();
    
    if (argc == 2) {
        // 没有指定 term，显示词典概览
        inspector.showLexiconSummary(20);
    } else {
        // 检查指定的 terms
        for (int i = 2; i < argc; i++) {
            inspector.inspectTerm(argv[i]);
        }
    }
    
    return 0;
}

