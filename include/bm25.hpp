#ifndef BM25_HPP
#define BM25_HPP

#include <cmath>
#include <cstdint>

// BM25 排序算法实现
// 参考：Robertson & Zaragoza, "The Probabilistic Relevance Framework: BM25 and Beyond"

namespace bm25 {

// BM25 参数
struct Params {
    double k1;  // 词频饱和参数，通常 0.8-1.2
    double b;   // 长度归一化参数，通常 0.3-0.7
    
    Params(double k1_val = 0.9, double b_val = 0.4) 
        : k1(k1_val), b(b_val) {}
};

// 计算 IDF (Inverse Document Frequency)
// N: 文档总数
// df: 包含该词的文档数
inline double idf(uint64_t N, uint32_t df) {
    if (df == 0 || N == 0) return 0.0;
    // Robertson/Spärck Jones IDF with +0.5 smoothing
    return std::log((static_cast<double>(N) - df + 0.5) / (df + 0.5) + 1.0);
}

// 计算单个词项对文档的 BM25 得分
// tf: 词项在文档中的频率
// dl: 文档长度
// avgdl: 平均文档长度
// params: BM25 参数
inline double score(uint32_t tf, uint32_t dl, double avgdl, const Params& params) {
    if (tf == 0 || dl == 0 || avgdl == 0.0) return 0.0;
    
    double tf_d = static_cast<double>(tf);
    double dl_d = static_cast<double>(dl);
    
    // BM25 公式：( tf * (k1 + 1) ) / ( tf + k1 * (1 - b + b * dl / avgdl) )
    double denominator = tf_d + params.k1 * (1.0 - params.b + params.b * dl_d / avgdl);
    return (tf_d * (params.k1 + 1.0)) / denominator;
}

// 计算完整的 BM25 得分（IDF * TF 分量）
// idf_val: 预计算的 IDF 值
// tf, dl, avgdl, params: 同上
inline double fullScore(double idf_val, uint32_t tf, uint32_t dl, double avgdl, const Params& params) {
    return idf_val * score(tf, dl, avgdl, params);
}

} // namespace bm25

#endif // BM25_HPP

