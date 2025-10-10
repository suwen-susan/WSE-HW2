#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>


// Posting结构
struct Posting {
    uint32_t docID;
    uint32_t frequency;

    Posting(uint32_t d, uint32_t f) : docID(d), frequency(f) {}
    
    bool operator<(const Posting& other) const {
        return docID < other.docID;
    }
};

// 词项和其postings
struct TermPostings {
    std::string term;
    std::vector<Posting> postings;
    
    TermPostings(const std::string& t) : term(t) {}
};

// 文本预处理：转小写、去标点
inline std::string normalize(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result.push_back(std::tolower(static_cast<unsigned char>(c)));
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            result.push_back(' '); // 保留空格分词
        }
    }
    return result;
}

// 分词
inline std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::stringstream ss(normalize(text));
    std::string token;
    while (ss >> token) {
        if (token.length() > 1) {  // 过滤单字符
            tokens.push_back(token);
        }
    }
    return tokens;
}
