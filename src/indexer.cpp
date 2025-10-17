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

/**
 * Helper function: Extract text snippet for document table
 * Handles special characters (tabs, newlines) to avoid breaking TSV format
 */
inline std::string makeSnippet(const std::string& text, size_t maxLen = 60) {
    std::string snippet;
    snippet.reserve(maxLen);
    
    size_t count = 0;
    for (unsigned char ch : text) {
        if (count >= maxLen) break;
        
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            if (!snippet.empty() && snippet.back() != ' ') {
                snippet.push_back(' ');
            }
        } else if (ch >= 32 || ch == '\t') {
            snippet.push_back(ch);
            count++;
        }
    }
    
    while (!snippet.empty() && snippet.back() == ' ') {
        snippet.pop_back();
    }
    
    return snippet;
}

/**
 * IndexBuilder: Phase 1 of the indexing pipeline
 * 
 * This class processes the raw document collection and generates:
 * 1. Document table mapping internal docIDs to original IDs
 * 2. Document content storage for snippet generation
 * 3. Flat posting files (term, docID, tf triples)
 * 
 * Features:
 * - Streaming processing for memory efficiency
 * - Automatic partitioning into multiple files to avoid single huge files
 * - Preserves all terms (no stopword filtering)
 */
class IndexBuilder {
private:
    uint32_t currentDocID;      // Current document ID being processed
    std::string outputDir;       // Output directory
    int batchNumber;             // Current partition number
    
    // Output file streams
    std::ofstream docTableFile;     // docID -> original ID mapping
    std::ofstream docContentFile;   // Full document content storage
    std::ofstream docOffsetFile;    // docID -> (offset, length) in content file
    
    std::ofstream postingsOut;      // Current posting partition file
    size_t partByteLimit;           // Byte threshold for each partition
    size_t bytesWrittenInPart;      // Bytes written in current partition
    size_t linesWrittenInPart;      // Lines written in current partition
    
    /**
     * Open a new posting partition file
     * Called when current partition exceeds size threshold
     */
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
    
    /**
     * Check if current partition exceeds size limit and rollover to new partition
     */
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
        
        fs::create_directories(outputDir);
        
        docTableFile.open(outputDir + "/doc_table.txt", std::ios::out);
        
        docContentFile.open(outputDir + "/doc_content.bin", 
                           std::ios::out | std::ios::binary);
        
        docOffsetFile.open(outputDir + "/doc_offset.bin", 
                          std::ios::out | std::ios::binary);
        
        if (!docTableFile.is_open() || !docContentFile.is_open() || 
            !docOffsetFile.is_open()) {
            std::cerr << "Failed to open doc_table.txt" << std::endl;
            exit(1);
        }
        
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
   
    /**
     * Parse a single document and generate postings
     * 
     * Steps:
     * 1. Write document table entry (docID -> original ID)
     * 2. Store full document content for later snippet generation
     * 3. Record content offset and length
     * 4. Tokenize document and compute term frequencies
     * 5. Write postings (term, docID, tf) to current partition
     */
    void parseDocument(const std::string& docName, const std::string& content) {
        // Write document table entry
        docTableFile << currentDocID << "\t" << docName << "\n";
        
        // Store document content and record offset
        uint64_t offset = docContentFile.tellp();
        
        // Clean content (remove tabs and newlines to keep single-line format)
        std::string cleanContent = content;
        std::replace(cleanContent.begin(), cleanContent.end(), '\t', ' ');
        std::replace(cleanContent.begin(), cleanContent.end(), '\n', ' ');
        std::replace(cleanContent.begin(), cleanContent.end(), '\r', ' ');
        
        docContentFile << cleanContent << "\n";
        
        uint64_t afterOffset = docContentFile.tellp();
        uint32_t length = static_cast<uint32_t>(afterOffset - offset - 1);
        
        // Write offset info: offset (8 bytes) + length (4 bytes)
        docOffsetFile.write(reinterpret_cast<const char*>(&offset), sizeof(uint64_t));
        docOffsetFile.write(reinterpret_cast<const char*>(&length), sizeof(uint32_t));
        
        // Compute term frequencies for this document
        std::unordered_map<std::string, uint32_t> termFreq;
        termFreq.reserve(256);  // Pre-allocate to reduce rehashing
        
        for (const auto& token : tokenize_words(content)) {
            termFreq[token]++;
        }
        
        // Write postings: term<TAB>docID<TAB>tf
        for (const auto& kv : termFreq) {
            const std::string& term = kv.first;
            uint32_t tf = kv.second;
            
            postingsOut << term << "\t" << currentDocID << "\t" << tf << "\n";
            
            // Estimate bytes written (avoid frequent tellp() calls for efficiency)
            bytesWrittenInPart += term.size() + 1 + 10 + 1 + 5 + 1;
            linesWrittenInPart++;
        }
        
        currentDocID++;
        
        // Check if we need to rollover to a new partition
        rolloverIfNeeded();
    }
    
    /**
     * Finalize indexing: close all files and print summary
     */
    void finalize() {
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
    
    /**
     * Process MS MARCO dataset format: TSV file (docID \t passage)
     * Streams through the input file and processes documents one by one
     */
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
            
            // Parse TSV format: docID \t passage
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
    
    size_t partSizeGB = 2;
    if (argc >= 4) {
        partSizeGB = std::stoull(argv[3]);
    }
    size_t partBytes = partSizeGB * 1024ULL * 1024ULL * 1024ULL;
    
    std::cout << "Building inverted index (Phase 1: Indexing)..." << std::endl;
    std::cout << "Input: " << inputFile << std::endl;
    std::cout << "Output: " << outputDir << std::endl;
    std::cout << "Part size: " << partSizeGB << " GB" << std::endl;
    
    IndexBuilder builder(outputDir, partBytes);
    
    builder.processMSMARCO(inputFile);
    
    std::cout << "\nIndex building phase 1 complete!" << std::endl;
    
    return 0;
}