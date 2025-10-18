#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <queue>
#include <algorithm>
#include <unordered_set>
#include <iomanip>
#include <chrono>
#include <memory>

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
    
    // ---- Parse arguments ----
    std::string indexDir = argv[1];
    std::string docTablePath = argv[2];
    
    std::string mode = "or";
    int defaultK = 10;
    double k1 = 0.9;
    double b = 0.4;
    
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--mode=") == 0) {
            mode = arg.substr(7);
            if (mode != "and" && mode != "or") mode = "or";
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
    std::cout << "Default mode: " << mode << std::endl;
    std::cout << "Default k: " << defaultK << std::endl;
    std::cout << "BM25 parameters: k1=" << k1 << ", b=" << b << std::endl;
    std::cout << std::endl;
    
    // ---- Load index ----
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

    // ---- Load document content ----
    DocContentFile docContent;
    std::string offsetPath = docTablePath;
    std::string contentPath = docTablePath;
    size_t lastSlash = offsetPath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        offsetPath = offsetPath.substr(0, lastSlash + 1) + "doc_offset.bin";
        contentPath = contentPath.substr(0, lastSlash + 1) + "doc_content.bin";
    } else {
        offsetPath = "doc_offset.bin";
        contentPath = "doc_content.bin";
    }
    
    if (!docContent.load(offsetPath, contentPath)) {
        std::cerr << "Warning: Could not load document content" << std::endl;
    }
    
    std::cout << "\nIndex loaded successfully!" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "\nEnter queries (one per line). Type /quit to exit." << std::endl;
    std::cout << "Use /and <query> or /or <query> to override mode for a single query.\n" << std::endl;
    
    // ---- Create Query Evaluator ----
    bm25::Params bm25Params(k1, b);
    QueryEvaluator evaluator(lexicon, stats, docLen, docTable, docContent, indexDir, bm25Params);
    
    /// ---- REPL----
    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) {
            break;
        }
        
        if (line.empty()) continue;
        
        // exit commands
        if (line == "/quit" || line == "/exit") {
            break;
        }
        
        std::string localMode = mode; 
        std::string query = line;
        
        if (line.find("/and ") == 0) {
            localMode = "and";
            query = line.substr(5);
        } else if (line.find("/or ") == 0) {
            localMode = "or";
            query = line.substr(4);
        }
        
        if (query.empty()) continue;
        
        auto start = std::chrono::high_resolution_clock::now();
        // ---- Load document content ----
        // tokenize
        std::vector<std::string> tokens = tokenize_words(query);
        
        if (tokens.empty()) {
            std::cout << "Empty query" << std::endl;
            continue;
        }
        
        // Remove duplicates
        std::unordered_set<std::string> uniqueSet(tokens.begin(), tokens.end());
        std::vector<std::string> queryTerms(uniqueSet.begin(), uniqueSet.end());
        
        std::cout << "Query terms: ";
        for (size_t i = 0; i < queryTerms.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << queryTerms[i];
        }
        std::cout << " (" << localMode << " mode)" << std::endl;
        
        // Evaluate query
        
        std::vector<QueryResult> results = evaluator.processQuery(queryTerms, localMode, defaultK);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // Output
        std::cout << "\nTop " << results.size() << " results (in " << duration.count() << " ms):\n";
        std::cout << std::string(80, '-') << std::endl;
        std::cout << std::setw(5) << "Rank" 
                  << std::setw(12) << "DocID" 
                  << std::setw(12) << "Score"
                  << "  Snippet" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        // Get document contents in batch
        std::vector<uint32_t> docIDs;
        for (const auto& r : results) {
            docIDs.push_back(r.docID);
        }
        
        auto contentStart = std::chrono::high_resolution_clock::now();
        auto contents = docContent.getBatch(docIDs);
        auto contentEnd = std::chrono::high_resolution_clock::now();
        auto contentDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            contentEnd - contentStart);
        
        std::cout << "(Content retrieval: " << contentDuration.count() << " ms)" << std::endl;
        
        for (size_t i = 0; i < results.size(); i++) {
            uint32_t docID = results[i].docID;
            
            std::cout << std::setw(3) << (i + 1) << ". "
                      << "Score: " << std::fixed << std::setprecision(4) << results[i].score
                      << " | DocID: " << docID 
                      << " | " << docTable.originalID(docID) << "\n";
            
            // generate query-dependent snippet
            auto it = contents.find(docID);
            if (it != contents.end() && !it->second.empty()) {
                std::string snippet = SnippetGenerator::generate(it->second, queryTerms);
                std::string highlighted = SnippetGenerator::highlight(snippet, queryTerms);
                std::cout << "    " << highlighted << "\n";
            }
            
            std::cout << std::string(100, '-') << std::endl;
        }
        
        if (results.empty()) {
            std::cout << "(No results found)" << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    std::cout << "\nGoodbye!" << std::endl;
    return 0;
}

