// Iris Service —— 句柄经纪协议实现
#include "service/ServiceProtocol.h"

#include <cstring>
#include <cwctype>

namespace iris::usnbroker {

namespace {

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));
    return s;
}

} // namespace

ParseResult ParseFrameHeader(const uint8_t* data, size_t avail, FrameHeader& out) {
    if (avail < kFrameHeaderSize) return ParseResult::NeedMore;
    const uint32_t magic = static_cast<uint32_t>(data[0]) |
                           (static_cast<uint32_t>(data[1]) << 8) |
                           (static_cast<uint32_t>(data[2]) << 16) |
                           (static_cast<uint32_t>(data[3]) << 24);
    if (magic != kFrameMagic) return ParseResult::BadMagic;
    const uint16_t ver = static_cast<uint16_t>(data[4] | (data[5] << 8));
    if (ver == 0 || ver > kProtoVer) return ParseResult::BadVersion;
    const uint16_t type = static_cast<uint16_t>(data[6] | (data[7] << 8));
    const uint32_t len = static_cast<uint32_t>(data[8]) |
                         (static_cast<uint32_t>(data[9]) << 8) |
                         (static_cast<uint32_t>(data[10]) << 16) |
                         (static_cast<uint32_t>(data[11]) << 24);
    if (len > kMaxPayload) return ParseResult::BadLength;
    out = FrameHeader{magic, ver, type, len};
    return ParseResult::Ok;
}

std::vector<uint8_t> EncodeFrame(uint16_t type, const void* payload, uint32_t payloadLen) {
    if (payloadLen > kMaxPayload) return {};
    std::vector<uint8_t> f(kFrameHeaderSize + payloadLen);
    f[0]  = static_cast<uint8_t>(kFrameMagic);
    f[1]  = static_cast<uint8_t>(kFrameMagic >> 8);
    f[2]  = static_cast<uint8_t>(kFrameMagic >> 16);
    f[3]  = static_cast<uint8_t>(kFrameMagic >> 24);
    f[4]  = static_cast<uint8_t>(kProtoVer);
    f[5]  = static_cast<uint8_t>(kProtoVer >> 8);
    f[6]  = static_cast<uint8_t>(type);
    f[7]  = static_cast<uint8_t>(type >> 8);
    f[8]  = static_cast<uint8_t>(payloadLen);
    f[9]  = static_cast<uint8_t>(payloadLen >> 8);
    f[10] = static_cast<uint8_t>(payloadLen >> 16);
    f[11] = static_cast<uint8_t>(payloadLen >> 24);
    if (payloadLen > 0) std::memcpy(f.data() + kFrameHeaderSize, payload, payloadLen);
    return f;
}

bool IsValidDriveLetter(uint16_t ch) {
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

bool IsAllowedClientPath(const std::wstring& clientExePath, const std::wstring& serviceExePath) {
    const size_t cSlash = clientExePath.find_last_of(L"\\/");
    const size_t sSlash = serviceExePath.find_last_of(L"\\/");
    if (cSlash == std::wstring::npos || sSlash == std::wstring::npos) return false;
    return ToLower(clientExePath.substr(cSlash + 1)) == L"iris.exe" &&
           ToLower(clientExePath.substr(0, cSlash)) == ToLower(serviceExePath.substr(0, sSlash));
}

} // namespace iris::usnbroker
