#pragma once

// UTF-8 <-> UTF-16. Invalid sequences become U+FFFD.

#include <cstdint>
#include <string>
#include <string_view>

namespace URK::Unreal {

inline std::string Utf16ToUtf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t code = text[i];
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (static_cast<std::uint32_t>(text[i + 1]) - 0xDC00);
            ++i;
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            code = 0xFFFD;
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

inline std::u16string Utf8ToUtf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t code = 0xFFFD;
        std::size_t length = 1;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
        }
        if (length > 1) {
            if (i + length > text.size()) {
                length = 1;
            } else {
                code = lead & (0xFF >> (length + 1));
                for (std::size_t k = 1; k < length; ++k) {
                    const auto next = static_cast<unsigned char>(text[i + k]);
                    if ((next & 0xC0) != 0x80) {
                        code = 0xFFFD;
                        length = k;
                        break;
                    }
                    code = (code << 6) | (next & 0x3F);
                }
                if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
                    code = 0xFFFD;
            }
        }
        if (code >= 0x10000) {
            code -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (code >> 10));
            out += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        } else {
            out += static_cast<char16_t>(code);
        }
        i += length;
    }
    return out;
}

} // namespace URK::Unreal
