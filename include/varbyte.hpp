#ifndef VARBYTE_HPP
#define VARBYTE_HPP

#include <cstdint>
#include <iostream>
#include <vector>

/**
 * @brief Variable-Byte (VarByte) encoding and decoding utility.
 *
 * Used to compress integer sequences such as docIDs and term frequencies
 * in inverted index postings.
 *
 * Each integer is divided into 7-bit chunks.
 * The MSB (most significant bit) of each byte acts as a continuation flag:
 *   - If MSB = 1 → this is the last byte of the number
 *   - If MSB = 0 → more bytes follow
 *
 * Example:
 *   824 = 0b1100111000  (binary)
 *   Split into 7-bit chunks: [0000110][0111000]
 *   Encode as bytes: 0x0E, 0xC8  (MSB=1 on last)
*/
namespace varbyte {

/**
 * @brief Encode a single integer into Variable-Byte code.
 * @param os Output stream to write encoded bytes           
 * @param value Integer to encode
 * @return void
 */
inline void encode(std::ostream& os, uint32_t value) {
    while (value >= 0x80) {
        unsigned char byte = static_cast<unsigned char>((value & 0x7F) | 0x80);
        os.put(static_cast<char>(byte));
        value >>= 7;
    }
    os.put(static_cast<char>(value & 0x7F));
}

/**
 * @brief Encode a sequence of integers into VarByte format.
 * @param buffer Output byte buffer to append encoded data
 * @param values Sequence of integers to encode
 * @return void
 */
inline void encode_batch(std::vector<unsigned char>& buffer, const std::vector<uint32_t>& values) {
    for (uint32_t value : values) {
        while (value >= 0x80) {
            buffer.push_back(static_cast<unsigned char>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        buffer.push_back(static_cast<unsigned char>(value & 0x7F));
    }
}


/**
 * @brief Decode a single integer from Variable-Byte code.
 * @param is Input stream containing VarByte encoded data
 * @return Decoded integer
 */
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

/**
 * @brief Decode a single integer directly from a byte pointer.
 * @param ptr Pointer to byte buffer
 * @return Decoded integer
 */
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

