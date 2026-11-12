#pragma once

#include <cstddef>
#include <string>

// Validation API host is stored as XOR-encoded bytes. The plaintext host does
// not appear in the dylib, logs, or the source header.
namespace endpoint {

inline std::string Host() {
    static const unsigned char enc[] = {
        0x36, 0xAF, 0xF5, 0x18, 0x03, 0x96, 0x01,
        0x6F, 0xDF, 0x75, 0x82, 0x01, 0xBB
    };
    static const unsigned char key[] = {
        0x5A, 0xC3, 0x91, 0x7E, 0x2D, 0xF4, 0x68, 0x0B,
        0xE5, 0x47, 0xB2, 0x39, 0x8C, 0xD1, 0x26, 0x73
    };
    std::string out;
    out.resize(sizeof(enc));
    for (std::size_t i = 0; i < sizeof(enc); ++i) {
        out[i] = static_cast<char>(enc[i] ^ key[i % sizeof(key)]);
    }
    return out;
}

inline std::string Base() {
    return "https://" + Host();
}

}  // namespace endpoint
