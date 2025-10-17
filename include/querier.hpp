#ifndef QUERIER_HPP
#define QUERIER_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <queue>
#include <algorithm>
#include <unordered_set>
#include <iomanip>
#include <chrono>
#include <cctype>  
#include "index_reader.hpp"
#include "bm25.hpp"
#include "utils.hpp"

/**
 * @brief A helper class for generating and highlighting query-dependent snippets.
 * 
 * This class extracts the most relevant segment of a document based on query terms,
 * optionally highlighting matched terms in the snippet.
*/
class SnippetGenerator {
private:
    static constexpr size_t SNIPPET_LENGTH = 200; // maximum snippet length (characters)
    static constexpr size_t CONTEXT_WINDOW = 50;  // context around matched term
    
public:
    /**
     * @brief Generate a text snippet containing the first query term found in the content.
     * 
     * @param content The full document text.
     * @param queryTerms List of query terms.
     * @return A string snippet with ellipses ("...") to indicate truncation.
    */
    static std::string generate(const std::string& content, 
                                const std::vector<std::string>& queryTerms) {
        if (content.empty() || queryTerms.empty()) {
            return truncate(content, SNIPPET_LENGTH);
        }
        
        // Find earliest occurrence of any query term
        size_t bestPos = std::string::npos;
        
        for (const auto& term : queryTerms) {
            size_t pos = findWholeWord(content, term);
            if (pos != std::string::npos && (bestPos == std::string::npos || pos < bestPos)) {
                bestPos = pos;
            }
        }
        
        // No term found → return beginning of content
        if (bestPos == std::string::npos) {
            return truncate(content, SNIPPET_LENGTH);
        }
        
        // Compute snippet start and end boundaries
        size_t start = (bestPos > CONTEXT_WINDOW) ? (bestPos - CONTEXT_WINDOW) : 0;
        size_t end = std::min(start + SNIPPET_LENGTH, content.size());
        
        // Adjust start to nearest sentence or word boundary
        if (start > 0) {
            // find nearest sentence or word boundary before start
            size_t sentenceStart = content.find_last_of(".!?\n", start);
            if (sentenceStart != std::string::npos && start - sentenceStart < 100) {
                start = sentenceStart + 1;
                // skip leading spaces
                while (start < content.size() && std::isspace(content[start])) {
                    start++;
                }
            } else {
                // find word boundary
                size_t wordStart = content.find_last_of(" \t\n", start);
                if (wordStart != std::string::npos && wordStart > 0) {
                    start = wordStart + 1;
                }
            }
        }
        
        if (end < content.size()) {
            // find nearest sentence or word boundary after end
            size_t sentenceEnd = content.find_first_of(".!?\n", end);
            if (sentenceEnd != std::string::npos && sentenceEnd - end < 100) {
                end = sentenceEnd + 1;
            } else {
                // find word boundary
                size_t wordEnd = content.find_first_of(" \t\n", end);
                if (wordEnd != std::string::npos) {
                    end = wordEnd;
                }
            }
        }
        
        // extract snippet
        std::string snippet = content.substr(start, end - start);
        
        // Trim leading/trailing whitespace
        size_t firstNonSpace = snippet.find_first_not_of(" \t\n\r");
        size_t lastNonSpace = snippet.find_last_not_of(" \t\n\r");
        if (firstNonSpace != std::string::npos && lastNonSpace != std::string::npos) {
            snippet = snippet.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
        }
        
        // Add ellipses for context
        if (start > 0) snippet = "..." + snippet;
        if (end < content.size()) snippet = snippet + "...";
        
        return snippet;
    }
    
    /**
     * @brief Highlight all query terms inside the snippet using ANSI escape codes.
     * 
     * @param snippet The snippet text to highlight.
     * @param queryTerms The list of query terms.
     * @return A snippet string with ANSI color codes applied.
    */
    static std::string highlight(const std::string& snippet, 
                                 const std::vector<std::string>& queryTerms) {
        // find all occurrences of query terms
        std::vector<std::pair<size_t, size_t>> matches;  // (start, length)
        
        for (const auto& term : queryTerms) {
            size_t pos = 0;
            while (pos < snippet.size()) {
                pos = findWholeWord(snippet, term, pos);
                if (pos == std::string::npos) break;
                
                matches.push_back({pos, term.length()});
                pos += term.length();
            }
        }
        
        if (matches.empty()) {
            return snippet;
        }
        
        // Remove overlapping matches
        std::sort(matches.begin(), matches.end());
        std::vector<std::pair<size_t, size_t>> uniqueMatches;
        
        for (const auto& match : matches) {
            bool overlap = false;
            for (const auto& existing : uniqueMatches) {
                if (match.first < existing.first + existing.second &&
                    match.first + match.second > existing.first) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) {
                uniqueMatches.push_back(match);
            }
        }
        
        // Insert ANSI color codes (from end to start to preserve indices)
        std::string result = snippet;
        for (auto it = uniqueMatches.rbegin(); it != uniqueMatches.rend(); ++it) {
            size_t pos = it->first;
            size_t len = it->second;
            
            result.insert(pos + len, "\033[0m");
            result.insert(pos, "\033[1;33m");
        }
        
        return result;
    }
    
private:
    /**
     * @brief Find a full-word match of a term in text (case-insensitive).
    */
    static size_t findWholeWord(const std::string& text, const std::string& word, size_t startPos = 0) {
        std::string lowerText = text;
        std::string lowerWord = word;
        
        // transform to lower case
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
        std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);
        
        size_t pos = startPos;
        while (pos < text.size()) {
            pos = lowerText.find(lowerWord, pos);
            if (pos == std::string::npos) break;
            
            // check boundaries
            bool validStart = (pos == 0 || !std::isalnum(static_cast<unsigned char>(text[pos - 1])));
            bool validEnd = (pos + word.length() >= text.size() || 
                           !std::isalnum(static_cast<unsigned char>(text[pos + word.length()])));
            
            if (validStart && validEnd) {
                return pos;
            }
            
            pos++;
        }
        
        return std::string::npos;
    }
    
    /**
     * @brief Truncate text to a fixed maximum length (cut at word boundary).
    */
    static std::string truncate(const std::string& text, size_t maxLen) {
        if (text.size() <= maxLen) return text;
        
        size_t cutPos = maxLen;
        size_t wordEnd = text.find_last_of(" \t\n", cutPos);
        if (wordEnd != std::string::npos && wordEnd > maxLen * 0.8) {
            cutPos = wordEnd;
        }
        
        return text.substr(0, cutPos) + "...";
    }
};


/**
 * @brief Represents a single ranked document in query results.
 * 
 * Each QueryResult stores:
 *  - docID: the internal document identifier
 *  - score: the relevance score (e.g., BM25)
 * 
 * The comparison operator is inverted so that it can be used
 * directly in a std::priority_queue as a min-heap for Top-K retrieval.
 */
struct QueryResult {
    uint32_t docID;
    double score;
    
    QueryResult(uint32_t d, double s) : docID(d), score(s) {}
    
    // min-heap based on score
    bool operator<(const QueryResult& other) const {
        return score > other.score;
    }
};

/**
 * @brief QueryEvaluator handles query processing, scoring, and ranking using BM25.
 * 
 * It uses lexicon and posting files to retrieve documents containing query terms.
 * Supports both AND and OR query modes and outputs ranked results.
*/
class QueryEvaluator {
private:
    Lexicon& lexicon;
    Stats& stats;

    const std::string& indexDir;
    DocLen& docLen;        // Document length manager
    DocTable& docTable;    // Document metadata (titles, IDs, etc.)
    DocContentFile& docContent; // File for reading document snippets

    bm25::Params bm25Params;
    std::ifstream docidsFile;
    std::ifstream freqsFile;


public:
    /**
     * @brief Constructor that initializes all references and opens posting files.
    */
    QueryEvaluator(Lexicon& lex, Stats& st, DocLen& dl, DocTable& dt, DocContentFile& dc,
                   const std::string& indexDir, bm25::Params params)
        : lexicon(lex), stats(st), docLen(dl), docTable(dt), docContent(dc), 
        indexDir(indexDir), bm25Params(params) {}

    /**
     * @brief Update BM25 parameters k1 and b.
    */
    void updateBM25Params(double k1, double b) {
        bm25Params = bm25::Params(k1, b);
    }
    
    /**
     * @brief Get current BM25 parameters.(Used for debugging)
    */
    bm25::Params getBM25Params() const {
        return bm25Params;
    }

    /**
     * @brief Process a given query string and return ranked results.
     * 
     * @param queryTerms The input query string.
     * @param mode Query mode: "and" or "or".
     * @param topK Number of results to return.
     * @return std::vector<QueryResult> Top-K ranked results.
     */
    std::vector<QueryResult> processQuery(const std::vector<std::string>& queryTerms, const std::string& mode, int k) {
        // Fetch posting lists and term metas for query terms
        std::vector<TermMeta> metas;
        std::vector<PostingList> lists;
        std::vector<double> idfs;
        
        for (const auto& term : queryTerms) {
            TermMeta meta;
            if (lexicon.find(term, meta)) {
                PostingList list;
                if (list.open(meta, indexDir)) {
                    metas.push_back(meta);
                    lists.push_back(std::move(list));
                    idfs.push_back(bm25::idf(stats.doc_count, meta.df));
                }
            }
        }
        
        if (lists.empty()) {
            return {};
        }

        std::string lowerMode = mode;
        std::transform(lowerMode.begin(), lowerMode.end(), lowerMode.begin(),
                    [](unsigned char c){ return std::tolower(c); });

        // Get Top-K results
        std::priority_queue<QueryResult> topK;
        if (mode == "and") {
            topK = evaluateAND(metas, lists, idfs, k);
        } else {
            topK = evaluateOR(metas, lists, idfs, k);
        }

        // Extract results from min-heap
        std::vector<QueryResult> results;
        while (!topK.empty()) {
            results.push_back(topK.top());
            topK.pop();
        }
        std::reverse(results.begin(), results.end());
        
        return results;
    }


private:
    /**
     * @brief Evaluate query in OR mode: documents should contain at least query terms.
    */
    std::priority_queue<QueryResult> evaluateOR(std::vector<TermMeta>& metas,
                                            std::vector<PostingList>& lists,
                                            std::vector<double>& idfs,
                                            int k) {
        // Top-K min-heap
        std::priority_queue<QueryResult> topK;
        
        // DAAT OR iteration
        while (true) {
            // find the minimum current docID among all lists
            uint32_t minDoc = UINT32_MAX;
            for (size_t i = 0; i < lists.size(); i++) {
                if (lists[i].valid() && lists[i].doc() < minDoc) {
                    minDoc = lists[i].doc();
                }
            }
            
            if (minDoc == UINT32_MAX) break;  // all lists exhausted
            
            // calculate BM25 score for minDoc
            double score = 0.0;
            uint32_t dl = docLen.len(minDoc);
            
            for (size_t i = 0; i < lists.size(); i++) {
                if (lists[i].valid() && lists[i].doc() == minDoc) {
                    uint32_t tf = lists[i].freq();
                    score += bm25::score(idfs[i], tf, dl, stats.avgdl, bm25Params);
                    lists[i].next();
                }
            }
            
            // update Top-K
            if (topK.size() < static_cast<size_t>(k)) {
                topK.push(QueryResult(minDoc, score));
            } else if (score > topK.top().score) {
                topK.pop();
                topK.push(QueryResult(minDoc, score));
            }
        }
        return topK;
    }
        
    /**
     * @brief Evaluate query in AND mode: documents must contain all query terms.
    */
    std::priority_queue<QueryResult> evaluateAND(std::vector<TermMeta>& metas,
                                        std::vector<PostingList>& lists,
                                        std::vector<double>& idfs,
                                        int k) {
        // Top-K min-heap
        std::priority_queue<QueryResult> topK;
        
        // DAAT AND iteration
        while (true) {
            // find the maximum current docID among all lists
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
            
            // push all lists to at least maxDoc
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
            
            if (!allMatch){
                for (auto& l : lists) l.nextGEQ(maxDoc + 1);
                continue;
            }
            
            // all lists match maxDoc → compute BM25 score
            double score = 0.0;
            uint32_t dl = docLen.len(maxDoc);
            
            for (size_t i = 0; i < lists.size(); i++) {
                uint32_t tf = lists[i].freq();
                score += bm25::score(idfs[i], tf, dl, stats.avgdl, bm25Params);
            }
            
            // update Top-K
            if (topK.size() < static_cast<size_t>(k)) {
                topK.push(QueryResult(maxDoc, score));
            } else if (score > topK.top().score) {
                topK.pop();
                topK.push(QueryResult(maxDoc, score));
            }
            
            // push all lists to next document
            for (size_t i = 0; i < lists.size(); i++) {
                lists[i].next();
            }
        }
        return topK;
    }

};

#endif // QUERIER_HPP