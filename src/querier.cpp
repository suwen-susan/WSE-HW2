#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <queue>
#include <algorithm>
#include <unordered_set>
#include <iomanip>
#include <chrono>
#include "index_reader.hpp"
#include "bm25.hpp"
#include "utils.hpp"
#include "querier.hpp"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <index_dir> <doc_table_path> [options]" << std::endl;
        std::cout << "\nOptions:" << std::endl;
        std::cout << "  --mode=and|or    Query mode (default: or)" << std::endl;
        std::cout << "  --k=N            Number of results (default: 10)" << std::endl;
        std::cout << "  --k1=X           BM25 k1 parameter (default: 0.9)" << std::endl;
        std::cout << "  --b=X            BM25 b parameter (default: 0.4)" << std::endl;
        std::cout << "\nExample:" << std::endl;
        std::cout << "  " << argv[0] << " ./index ./output/doc_table.txt --mode=or --k=10" << std::endl;
        std::cout << "\nInteractive commands:" << std::endl;
        std::cout << "  /and <query>     Switch to AND mode for this query" << std::endl;
        std::cout << "  /or <query>      Switch to OR mode for this query" << std::endl;
        std::cout << "  /quit or /exit   Exit the program" << std::endl;
        return 1;
    }
    
    std::string indexDir = argv[1];
    std::string docTablePath = argv[2];
    
    // 解析参数
    bool defaultConjunctive = false;
    int defaultK = 10;
    double k1 = 0.9;
    double b = 0.4;
    
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--mode=") == 0) {
            std::string mode = arg.substr(7);
            defaultConjunctive = (mode == "and");
        } else if (arg.find("--k=") == 0) {
            defaultK = std::stoi(arg.substr(4));
        } else if (arg.find("--k1=") == 0) {
            k1 = std::stod(arg.substr(5));
        } else if (arg.find("--b=") == 0) {
            b = std::stod(arg.substr(4));
        }
    }
    
    std::cout << "Query Processor (Phase 3)" << std::endl;
    std::cout << "=========================" << std::endl;
    std::cout << "Index directory: " << indexDir << std::endl;
    std::cout << "Doc table: " << docTablePath << std::endl;
    std::cout << "Default mode: " << (defaultConjunctive ? "AND" : "OR") << std::endl;
    std::cout << "Default k: " << defaultK << std::endl;
    std::cout << "BM25 parameters: k1=" << k1 << ", b=" << b << std::endl;
    std::cout << std::endl;
    
    // 加载索引
    std::cout << "Loading index..." << std::endl;
    
    Lexicon lexicon;
    if (!lexicon.load(indexDir + "/lexicon.tsv")) {
        return 1;
    }
    
    Stats stats;
    if (!stats.load(indexDir + "/stats.txt")) {
        return 1;
    }
    
    DocLen docLen;
    if (!docLen.load(indexDir + "/doc_len.bin")) {
        return 1;
    }
    
    DocTable docTable;
    if (!docTable.load(docTablePath)) {
        return 1;
    }
    
    std::cout << "\nIndex loaded successfully!" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "\nEnter queries (one per line). Type /quit to exit." << std::endl;
    std::cout << "Use /and <query> or /or <query> to override mode for a single query.\n" << std::endl;
    
    // 创建查询评估器
    bm25::Params bm25Params(k1, b);
    QueryEvaluator evaluator(lexicon, stats, docLen, docTable, indexDir, bm25Params);
    
    // REPL
    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) {
            break;
        }
        
        if (line.empty()) continue;
        
        // 处理命令
        if (line == "/quit" || line == "/exit") {
            break;
        }
        
        bool conjunctive = defaultConjunctive;
        std::string query = line;
        
        if (line.find("/and ") == 0) {
            conjunctive = true;
            query = line.substr(5);
        } else if (line.find("/or ") == 0) {
            conjunctive = false;
            query = line.substr(4);
        }
        
        if (query.empty()) continue;
        
        evaluator.processQuery(query, conjunctive, defaultK);
    }
    
    std::cout << "\nGoodbye!" << std::endl;
    return 0;
}

