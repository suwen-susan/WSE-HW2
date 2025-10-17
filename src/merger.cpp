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

/**
 * IndexMerger: Merges sorted postings into a compressed inverted index
 * 
 * This class implements Phase 2 of the indexing pipeline. It reads sorted postings
 * from Phase 1 and creates a block-compressed inverted index with:
 * - Block-compressed docIDs (gap-encoded with VarByte)
 * - Block-compressed frequencies (VarByte encoded)
 * - Lexicon mapping terms to posting list offsets
 * - Index statistics for BM25 scoring
 */
class IndexMerger {
private:
    // Block size for compression (number of postings per block)
    static constexpr size_t BLOCK_SIZE = 128;
    // Buffer size for efficient I/O operations
    static constexpr size_t READ_BUFFER_SIZE = 8 * 1024 * 1024;
    
    // Input/output paths
    std::string inputFile;      // Sorted postings file from Phase 1
    std::string outputDir;      // Output directory for index files
    
    // Output file streams
    std::ofstream docIdsFile;   // Block-compressed docIDs
    std::ofstream freqsFile;    // Block-compressed frequencies
    std::ofstream lexiconFile;  // Term dictionary (text format for debugging)
    std::ofstream statsFile;    // Index statistics
    
    // Statistics accumulators
    uint64_t totalTerms;        // Total number of unique terms
    uint64_t totalPostings;     // Total number of postings
    uint64_t docCount;          // Total number of documents
    std::vector<uint32_t> docLengths;  // Document lengths for BM25 avgdl
    
    // Posting structure for in-memory representation
    struct Posting {
        uint32_t docID;
        uint32_t frequency;
        
        Posting(uint32_t d, uint32_t f) : docID(d), frequency(f) {}
    };
    
public:
    IndexMerger(const std::string& input, const std::string& outDir)
        : inputFile(input), outputDir(outDir),
          totalTerms(0), totalPostings(0), docCount(0) {
        
        fs::create_directories(outputDir);
        
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
        
        lexiconFile << "# term\tdf\tcf\tdocids_offset\tfreqs_offset\tblocks_count\n";
    }
    
    ~IndexMerger() {
        if (docIdsFile.is_open()) docIdsFile.close();
        if (freqsFile.is_open()) freqsFile.close();
        if (lexiconFile.is_open()) lexiconFile.close();
        if (statsFile.is_open()) statsFile.close();
    }
    
    /**
     * Main processing pipeline: reads sorted postings and writes compressed index
     * 
     * Algorithm:
     * 1. Stream through sorted postings line by line
     * 2. Group postings by term (postings are sorted by term, then by docID)
     * 3. For each complete term, write its inverted list in compressed blocks
     * 4. Generate lexicon entries and statistics
     */
    void process() {
        std::ifstream inFile(inputFile);
        if (!inFile.is_open()) {
            std::cerr << "Cannot open input file: " << inputFile << std::endl;
            exit(1);
        }
        
        // Set read buffer for efficient I/O
        std::vector<char> readBuffer(READ_BUFFER_SIZE);
        inFile.rdbuf()->pubsetbuf(readBuffer.data(), READ_BUFFER_SIZE);
        
        std::cout << "Merging sorted postings into compressed index..." << std::endl;
        std::cout << "Input: " << inputFile << std::endl;
        std::cout << "Output: " << outputDir << std::endl;
        std::cout << "Block size: " << BLOCK_SIZE << std::endl;
        
        // Streaming processing: read line by line, group by term
        std::string line;
        std::string currentTerm;
        std::vector<Posting> currentPostings;
        currentPostings.reserve(1024);  // Pre-allocate for efficiency
        
        uint64_t linesProcessed = 0;
        
        while (std::getline(inFile, line)) {
            if (line.empty() || line[0] == '#') continue;  // Skip empty lines and comments
            
            size_t tab1 = line.find('\t');
            size_t tab2 = line.find('\t', tab1 + 1);
            
            if (tab1 == std::string::npos || tab2 == std::string::npos) {
                std::cerr << "Warning: malformed line: " << line << std::endl;
                continue;
            }
            
            // Parse line: term<TAB>docID<TAB>tf
            std::string term = line.substr(0, tab1);
            uint32_t docID = std::stoul(line.substr(tab1 + 1, tab2 - tab1 - 1));
            uint32_t tf = std::stoul(line.substr(tab2 + 1));
            
            // Update document count
            if (docID >= docCount) {
                docCount = docID + 1;
            }
            
            // Check if we've moved to a new term
            if (term != currentTerm) {
                if (!currentPostings.empty()) {
                    // Write out the inverted list for the previous term
                    writeInvertedList(currentTerm, currentPostings);
                    currentPostings.clear();
                }
                currentTerm = term;
            }
            
            // Accumulate posting for current term
            currentPostings.emplace_back(docID, tf);
            
            linesProcessed++;
            if (linesProcessed % 10000000 == 0) {
                std::cout << "Processed " << (linesProcessed / 1000000) 
                          << "M postings, " << totalTerms << " terms..." << std::endl;
            }
        }
        
        // Write out the last term
        if (!currentPostings.empty()) {
            writeInvertedList(currentTerm, currentPostings);
        }
        
        inFile.close();
        
        // Write statistics and document lengths
        writeStats();
        
        std::cout << "\nMerging complete!" << std::endl;
        std::cout << "Total terms: " << totalTerms << std::endl;
        std::cout << "Total postings: " << totalPostings << std::endl;
        std::cout << "Total documents: " << docCount << std::endl;
    }
    
private:
    /**
     * Write inverted list for a single term using block compression
     * 
     * Format:
     * - docIDs: block_size + gap-encoded docID sequence (VarByte)
     * - frequencies: block_size + tf sequence (VarByte)
     * - lexicon: term metadata (df, cf, offsets, block count)
     */
    void writeInvertedList(const std::string& term, const std::vector<Posting>& postings) {
        if (postings.empty()) return;
        
        // Record file offsets before writing
        uint64_t docIdsOffset = docIdsFile.tellp();
        uint64_t freqsOffset = freqsFile.tellp();
        
        // Calculate statistics
        uint32_t df = static_cast<uint32_t>(postings.size());  // Document frequency
        uint64_t cf = 0;  // Collection frequency (sum of all tf)
        
        // Write in blocks for compression efficiency
        size_t blocksCount = 0;
        for (size_t i = 0; i < postings.size(); i += BLOCK_SIZE) {
            size_t blockLen = std::min(BLOCK_SIZE, postings.size() - i);
            
            writeDocIDsBlock(postings, i, blockLen);
            
            writeFrequenciesBlock(postings, i, blockLen, cf);
            
            blocksCount++;
        }
        
        lexiconFile << term << "\t" 
                    << df << "\t" 
                    << cf << "\t"
                    << docIdsOffset << "\t" 
                    << freqsOffset << "\t"
                    << blocksCount << "\n";
        
        totalTerms++;
        totalPostings += df;
    }
    
    /**
     * Write docIDs block with gap encoding and VarByte compression
     * 
     * Block format: block_length + docID_sequence
     * docID_sequence uses gap encoding: first docID is absolute,
     * subsequent docIDs are stored as gaps (docID[i] - docID[i-1])
     */
    void writeDocIDsBlock(const std::vector<Posting>& postings, 
                          size_t start, size_t length) {
        varbyte::encode(docIdsFile, static_cast<uint32_t>(length));
        
        uint32_t prevDocID = 0;
        for (size_t i = 0; i < length; i++) {
            uint32_t docID = postings[start + i].docID;
            uint32_t gap = (i == 0) ? docID : (docID - prevDocID);
            varbyte::encode(docIdsFile, gap);
            prevDocID = docID;
        }
    }
    
    /**
     * Write frequencies block with VarByte compression
     * 
     * Block format: block_length + tf_sequence
     * Also updates collection frequency (cf) and document lengths
     * for BM25 avgdl calculation
     */
    void writeFrequenciesBlock(const std::vector<Posting>& postings, 
                               size_t start, size_t length, uint64_t& cf) {
        varbyte::encode(freqsFile, static_cast<uint32_t>(length));
        
        for (size_t i = 0; i < length; i++) {
            uint32_t tf = postings[start + i].frequency;
            varbyte::encode(freqsFile, tf);
            cf += tf;
            
            // Update document length for BM25 avgdl calculation
            uint32_t docID = postings[start + i].docID;
            if (docID >= docLengths.size()) {
                docLengths.resize(docID + 1, 0);
            }
            docLengths[docID] += tf;
        }
    }
    
    /**
     * Write statistics file and document lengths file
     * 
     * Outputs:
     * - doc_len.bin: binary file with document lengths (needed for BM25)
     * - stats.txt: text file with index statistics (doc_count, avgdl, etc.)
     */
    void writeStats() {
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
        
        statsFile.open(outputDir + "/stats.txt", std::ios::out);
        if (!statsFile.is_open()) {
            std::cerr << "Failed to open stats file" << std::endl;
            return;
        }
        
        uint64_t totalDocLength = 0;
        for (uint32_t len : docLengths) {
            totalDocLength += len;
        }
        double avgdl = (docCount > 0) ? static_cast<double>(totalDocLength) / docCount : 0.0;
        
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

