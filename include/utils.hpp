#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

// 文本预处理：非字母数字统一视为分隔符；转小写；不做词干化
// 符合课程建议：'-', ':', '.', ',', '(', ')', 等都是分隔符
inline std::string normalize(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    
    auto push_space = [&]() {
        if (!result.empty() && result.back() != ' ') {
            result.push_back(' ');
        }
    };
    
    for (unsigned char uc : text) {
        if (std::isalnum(uc)) {
            result.push_back(std::tolower(uc));
        } else {
            // 任何非字母数字字符都是分隔符
            push_space();
        }
    }
    return result;
}

// 分词：保留所有词（包括数字、单字符、停用词）
// 符合课程建议：不过滤停用词、保留数字词如 "2"、"7up"、"smith007"
inline std::vector<std::string> tokenize_words(const std::string& text) {
    std::vector<std::string> tokens;
    std::stringstream ss(normalize(text));
    std::string token;
    while (ss >> token) {
        // 不做任何过滤，保留所有分割出的词
        tokens.push_back(token);
    }
    return tokens;
}
