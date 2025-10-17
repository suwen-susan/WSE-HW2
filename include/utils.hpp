#ifndef UTILS_HPP
#define UTILS_HPP

#include <vector>
#include <string>
#include <cctype>

inline std::vector<std::string> tokenize_words(const std::string& text) {
    std::vector<std::string> tokens;
    std::string cur;
    cur.reserve(text.size());      

    for (unsigned char uc : text) {     
        if (std::isalnum(uc)) {
            cur.push_back(std::tolower(uc));
        } else if (!cur.empty()) {      
            tokens.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) tokens.push_back(std::move(cur));
    return tokens;
}

#endif // UTILS_HPP
