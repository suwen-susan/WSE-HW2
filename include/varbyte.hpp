#ifndef VARBYTE_HPP
#define VARBYTE_HPP

#include <cstdint>
#include <iostream>
#include <vector>

// VarByte 编码：变长整数编码，节省空间
// 每个字节的最高位标识是否还有后续字节
// 适用于大部分数值较小的场景（如 docID gap、tf）

namespace varbyte {

// 编码单个整数到输出流
inline void encode(std::ostream& os, uint32_t value) {
    while (value >= 0x80) {
        unsigned char byte = static_cast<unsigned char>((value & 0x7F) | 0x80);
        os.put(static_cast<char>(byte));
        value >>= 7;
    }
    os.put(static_cast<char>(value & 0x7F));
}

// 编码整数数组到缓冲区（批量操作更高效）
inline void encode_batch(std::vector<unsigned char>& buffer, const std::vector<uint32_t>& values) {
    for (uint32_t value : values) {
        while (value >= 0x80) {
            buffer.push_back(static_cast<unsigned char>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        buffer.push_back(static_cast<unsigned char>(value & 0x7F));
    }
}

// 解码单个整数（第三阶段查询时使用）
inline uint32_t decode(std::istream& is) {
    uint32_t value = 0;
    unsigned int shift = 0;
    unsigned char byte;
    
    do {
        byte = static_cast<unsigned char>(is.get());
        value |= static_cast<uint32_t>(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    
    return value;
}

// 从字节缓冲区解码（第三阶段查询时使用）
inline uint32_t decode_from_buffer(const unsigned char*& ptr) {
    uint32_t value = 0;
    unsigned int shift = 0;
    unsigned char byte;
    
    do {
        byte = *ptr++;
        value |= static_cast<uint32_t>(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    
    return value;
}

} // namespace varbyte

#endif // VARBYTE_HPP

