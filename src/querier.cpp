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

// 查询结果（用于 Top-K）
struct QueryResult {
    uint32_t docID;
    double score;
    
    QueryResult(uint32_t d, double s) : docID(d), score(s) {}
    
    // 最小堆需要反向比较
    bool operator<(const QueryResult& other) const {
        return score > other.score;
    }
};

// 查询评估器
class QueryEvaluator {
private:
    Lexicon& lexicon;
    Stats& stats;
    DocLen& docLen;
    DocTable& docTable;
    bm25::Params bm25Params;
    
    std::ifstream docidsFile;
    std::ifstream freqsFile;
    
public:
    QueryEvaluator(Lexicon& lex, Stats& st, DocLen& dl, DocTable& dt, 
                   const std::string& indexDir, bm25::Params params)
        : lexicon(lex), stats(st), docLen(dl), docTable(dt), bm25Params(params) {
        
        // 打开倒排表文件
        docidsFile.open(indexDir + "/postings.docids.bin", std::ios::binary);
        freqsFile.open(indexDir + "/postings.freqs.bin", std::ios::binary);
        
        if (!docidsFile.is_open() || !freqsFile.is_open()) {
            std::cerr << "Failed to open posting files" << std::endl;
            exit(1);
        }
    }
    
    ~QueryEvaluator() {
        if (docidsFile.is_open()) docidsFile.close();
        if (freqsFile.is_open()) freqsFile.close();
    }
    
    // 析取查询（OR）- DAAT
    std::vector<QueryResult> evaluateOR(const std::vector<std::string>& queryTerms, int k) {
        // 查找词项并打开倒排表
        std::vector<TermMeta> metas;
        std::vector<PostingList> lists;
        std::vector<double> idfs;
        
        for (const auto& term : queryTerms) {
            TermMeta meta;
            if (lexicon.find(term, meta)) {
                PostingList list;
                if (list.open(meta, docidsFile, freqsFile)) {
                    metas.push_back(meta);
                    lists.push_back(std::move(list));
                    idfs.push_back(bm25::idf(stats.doc_count, meta.df));
                }
            }
        }
        
        if (lists.empty()) {
            return {};
        }
        
        // Top-K 最小堆
        std::priority_queue<QueryResult> topK;
        
        // DAAT 遍历
        while (true) {
            // 找到当前最小的 docID
            uint32_t minDoc = UINT32_MAX;
            for (size_t i = 0; i < lists.size(); i++) {
                if (lists[i].valid() && lists[i].doc() < minDoc) {
                    minDoc = lists[i].doc();
                }
            }
            
            if (minDoc == UINT32_MAX) break;  // 所有列表已遍历完
            
            // 计算当前文档的 BM25 得分
            double score = 0.0;
            uint32_t dl = docLen.len(minDoc);
            
            for (size_t i = 0; i < lists.size(); i++) {
                if (lists[i].valid() && lists[i].doc() == minDoc) {
                    uint32_t tf = lists[i].freq();
                    score += bm25::fullScore(idfs[i], tf, dl, stats.avgdl, bm25Params);
                    lists[i].next();
                }
            }
            
            // 更新 Top-K
            if (topK.size() < static_cast<size_t>(k)) {
                topK.push(QueryResult(minDoc, score));
            } else if (score > topK.top().score) {
                topK.pop();
                topK.push(QueryResult(minDoc, score));
            }
        }
        
        // 提取结果并排序
        std::vector<QueryResult> results;
        while (!topK.empty()) {
            results.push_back(topK.top());
            topK.pop();
        }
        std::reverse(results.begin(), results.end());
        
        return results;
    }
    
    // 合取查询（AND）- DAAT
    std::vector<QueryResult> evaluateAND(const std::vector<std::string>& queryTerms, int k) {
        // 查找词项并打开倒排表
        std::vector<TermMeta> metas;
        std::vector<PostingList> lists;
        std::vector<double> idfs;
        
        for (const auto& term : queryTerms) {
            TermMeta meta;
            if (lexicon.find(term, meta)) {
                PostingList list;
                if (list.open(meta, docidsFile, freqsFile)) {
                    metas.push_back(meta);
                    lists.push_back(std::move(list));
                    idfs.push_back(bm25::idf(stats.doc_count, meta.df));
                }
            }
        }
        
        if (lists.empty()) {
            return {};
        }
        
        // Top-K 最小堆
        std::priority_queue<QueryResult> topK;
        
        // DAAT 合取遍历
        while (true) {
            // 找到当前最大的 docID（作为候选）
            uint32_t maxDoc = 0;
            bool allValid = true;
            
            for (size_t i = 0; i < lists.size(); i++) {
                if (!lists[i].valid()) {
                    allValid = false;
                    break;
                }
                if (lists[i].doc() > maxDoc) {
                    maxDoc = lists[i].doc();
                }
            }
            
            if (!allValid) break;
            
            // 将所有列表推进到 >= maxDoc
            bool allMatch = true;
            for (size_t i = 0; i < lists.size(); i++) {
                if (lists[i].doc() < maxDoc) {
                    if (!lists[i].nextGEQ(maxDoc)) {
                        allMatch = false;
                        break;
                    }
                }
                if (lists[i].doc() != maxDoc) {
                    allMatch = false;
                    break;
                }
            }
            
            if (!allMatch) continue;
            
            // 所有列表都命中 maxDoc，计算 BM25 得分
            double score = 0.0;
            uint32_t dl = docLen.len(maxDoc);
            
            for (size_t i = 0; i < lists.size(); i++) {
                uint32_t tf = lists[i].freq();
                score += bm25::fullScore(idfs[i], tf, dl, stats.avgdl, bm25Params);
            }
            
            // 更新 Top-K
            if (topK.size() < static_cast<size_t>(k)) {
                topK.push(QueryResult(maxDoc, score));
            } else if (score > topK.top().score) {
                topK.pop();
                topK.push(QueryResult(maxDoc, score));
            }
            
            // 推进所有列表
            for (size_t i = 0; i < lists.size(); i++) {
                lists[i].next();
            }
        }
        
        // 提取结果并排序
        std::vector<QueryResult> results;
        while (!topK.empty()) {
            results.push_back(topK.top());
            topK.pop();
        }
        std::reverse(results.begin(), results.end());
        
        return results;
    }
    
    // 处理查询
    void processQuery(const std::string& query, bool conjunctive, int k) {
        // 分词（使用与索引时相同的分词规则）
        std::vector<std::string> tokens = tokenize_words(query);
        
        if (tokens.empty()) {
            std::cout << "Empty query" << std::endl;
            return;
        }
        
        // 去重
        std::unordered_set<std::string> uniqueSet(tokens.begin(), tokens.end());
        std::vector<std::string> queryTerms(uniqueSet.begin(), uniqueSet.end());
        
        std::cout << "Query terms: ";
        for (size_t i = 0; i < queryTerms.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << queryTerms[i];
        }
        std::cout << " (" << (conjunctive ? "AND" : "OR") << " mode)" << std::endl;
        
        // 评估查询
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<QueryResult> results;
        if (conjunctive) {
            results = evaluateAND(queryTerms, k);
        } else {
            results = evaluateOR(queryTerms, k);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // 显示结果
        std::cout << "\nTop " << results.size() << " results (in " << duration.count() << " ms):\n";
        std::cout << std::string(80, '-') << std::endl;
        std::cout << std::setw(5) << "Rank" 
                  << std::setw(12) << "DocID" 
                  << std::setw(12) << "Score"
                  << "  Document" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        for (size_t i = 0; i < results.size(); i++) {
            std::cout << std::setw(5) << (i + 1)
                      << std::setw(12) << results[i].docID
                      << std::setw(12) << std::fixed << std::setprecision(4) << results[i].score
                      << "  " << docTable.name(results[i].docID) << std::endl;
        }
        
        if (results.empty()) {
            std::cout << "(No results found)" << std::endl;
        }
        
        std::cout << std::endl;
    }
};

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

