#pragma once

#include <cstdint>
#include <string>

namespace opitcore {
namespace security {
namespace secrets {

// Secret obfuscado (XOR ciclico multi-byte): tira as strings-alvo do binario em claro
// (scan de `strings` e grep de patterns). Decorativo contra reverser determinado - a
// barreira e apenas contra ferramentas passivas.
static const uint8_t k_manifest_file[] = { 0x35, 0x4C, 0xF8, 0x93, 0x7B, 0xD5, 0x0C, 0x9E, 0x20, 0xC4, 0x1B, 0x62, 0xA7, 0x76, 0xAC, 0xF8, 0x35, 0x52 };
static const uint8_t k_manifest_sig[] = { 0x35, 0x4C, 0xF8, 0x93, 0x7B, 0xD5, 0x0C, 0x9E, 0x20, 0xC4, 0x1B, 0x62, 0xA7, 0x76, 0xB5, 0xE2, 0x3D };
static const uint8_t k_key[] = { 0x5A, 0x3C, 0x91, 0xE7, 0x24, 0xB8, 0x6D, 0xF0, 0x49, 0xA2, 0x7E, 0x11, 0xD3, 0x58, 0xC6, 0x8B };

inline std::string unpack(const uint8_t* data, size_t len, const uint8_t* key, size_t klen) {
    std::string out(len, '\0');
    for (size_t i = 0; i < len; ++i) out[i] = static_cast<char>(data[i] ^ key[i % klen]);
    return out;
}

inline std::string manifest_file_name() {
    return unpack(k_manifest_file, sizeof(k_manifest_file), k_key, sizeof(k_key));
}

inline std::string manifest_sig_name() {
    return unpack(k_manifest_sig, sizeof(k_manifest_sig), k_key, sizeof(k_key));
}

} // namespace secrets
} // namespace security
} // namespace opitcore
