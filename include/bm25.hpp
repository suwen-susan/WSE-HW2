#ifndef BM25_HPP
#define BM25_HPP

#include <cmath>
#include <cstdint>

// BM25 scoring functions
// Reference：Robertson & Zaragoza, "The Probabilistic Relevance Framework: BM25 and Beyond"
namespace bm25 {

/**
     * @brief Parameters for the BM25 ranking function.
     * 
     * k1: Term frequency saturation parameter (usually 0.8–1.2)
     * b:  Document length normalization parameter (usually 0.3–0.7)
*/
struct Params {
    double k1; 
    double b; 
    
    Params(double k1_val = 0.9, double b_val = 0.4) 
        : k1(k1_val), b(b_val) {}
};

/**
     * @brief Compute Inverse Document Frequency (IDF)
     * 
     * @param N  Total number of documents
     * @param df Number of documents containing the term
     * @return double The computed IDF value
     * 
     * The formula used is the Robertson–Sparck Jones version with +0.5 smoothing:
     *     idf = log( (N - df + 0.5) / (df + 0.5) + 1 )
*/
inline double idf(uint64_t N, uint32_t df) {
    if (df == 0 || N == 0) return 0.0;
    // Robertson/Spärck Jones IDF with +0.5 smoothing
    return std::log((static_cast<double>(N) - df + 0.5) / (df + 0.5) + 1.0);
}

/**
     * @brief Compute the BM25 score for a single term in a document.
     * 
     * @param tf    Term frequency in the document
     * @param idf   The computed IDF value
     * @param dl    Current document length
     * @param avgdl Average document length across the collection
     * @param params BM25 parameters (k1, b)
     * @return double BM25 score for this term in the given document
     * 
     * The core BM25 formula:
     *     score = idf * ((tf * (k1 + 1)) / (tf + k1 * (1 - b + b * dl / avgdl)))
*/
inline double score(double idf_val, uint32_t tf, uint32_t dl, double avgdl, const Params& params) {
    if (tf == 0 || dl == 0 || avgdl == 0.0) 
        return 0.0;

    double tf_d = static_cast<double>(tf);
    double dl_d = static_cast<double>(dl);
    
    double numerator = tf_d * (params.k1 + 1.0);
    double denominator = tf_d + params.k1 * (1.0 - params.b + params.b * dl_d / avgdl);
    return idf_val * (numerator / denominator);
}

} // namespace bm25

#endif // BM25_HPP
