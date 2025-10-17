#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

/**
 * @brief Normalize text by converting to lowercase and replacing non-alphanumeric characters with spaces.
 * 
 * @param text Input text.
 * @return Normalized text.
*/
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
            // Replace non-alphanumeric with space
            push_space();
        }
    }
    return result;
}

/**
 * @brief Split a string into tokens using whitespace and punctuation.
 * 
 * @param text The input text.
 * @return Vector of tokens (words).
*/
inline std::vector<std::string> tokenize_words(const std::string& text) {
    std::vector<std::string> tokens;
    std::stringstream ss(normalize(text));
    std::string token;
    while (ss >> token) {
        // keep all tokens
        tokens.push_back(token);
    }
    return tokens;
}

#endif // UTILS_HPP
