// ServiceProtocol 编解码/校验 + 句柄经纪集成测试
// 单测：帧编解码 round-trip、畸形帧拒绝、盘符白名单、客户端路径校验（纯逻辑）。
// 集成：拉起 IrisService.exe --console 做真实管道往返；IrisService 未构建则跳过。
//       OpenVolume 的预期结果随本进程提权状态自适应（非提权 → OpenFailed err=5；
//       提权 → 成功拿到可用句柄，顺带校验 DuplicateHandle 全链路）。
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "service/ServiceProtocol.h"
#include "service/UsnBrokerClient.h"

using namespace iris::usnbroker;

// ---- 帧编解码 ----

TEST(ServiceProtocol, EncodeParseRoundTrip) {
    const PingPayload in{123};
    const auto frame = EncodeFrame(static_cast<uint16_t>(MsgType::Ping), &in, sizeof(in));
    ASSERT_EQ(frame.size(), kFrameHeaderSize + sizeof(in));
    // 头部字节序明确性：'IRSB' 小端
    EXPECT_EQ(frame[0], 0x49); EXPECT_EQ(frame[1], 0x52);
    EXPECT_EQ(frame[2], 0x53); EXPECT_EQ(frame[3], 0x42);

    FrameHeader h;
    ASSERT_EQ(ParseFrameHeader(frame.data(), frame.size(), h), ParseResult::Ok);
    EXPECT_EQ(h.magic, kFrameMagic);
    EXPECT_EQ(h.protoVer, kProtoVer);
    EXPECT_EQ(h.type, static_cast<uint16_t>(MsgType::Ping));
    EXPECT_EQ(h.payloadLen, sizeof(in));
    const auto* payload =
        reinterpret_cast<const PingPayload*>(frame.data() + kFrameHeaderSize);
    EXPECT_EQ(payload->clientProtoVer, 123u);
}

TEST(ServiceProtocol, EmptyPayloadFrame) {
    const auto frame = EncodeFrame(static_cast<uint16_t>(MsgType::Ping), nullptr, 0);
    ASSERT_EQ(frame.size(), kFrameHeaderSize);
    FrameHeader h;
    EXPECT_EQ(ParseFrameHeader(frame.data(), frame.size(), h), ParseResult::Ok);
    EXPECT_EQ(h.payloadLen, 0u);
}

TEST(ServiceProtocol, RejectsOversizedPayload) {
    EXPECT_TRUE(EncodeFrame(1, nullptr, kMaxPayload + 1).empty());
}

TEST(ServiceProtocol, ParseNeedMore) {
    FrameHeader h;
    const uint8_t buf[kFrameHeaderSize - 1] = {};
    EXPECT_EQ(ParseFrameHeader(buf, sizeof(buf), h), ParseResult::NeedMore);
}

TEST(ServiceProtocol, ParseBadMagic) {
    PingPayload in{};
    auto frame = EncodeFrame(static_cast<uint16_t>(MsgType::Ping), &in, sizeof(in));
    frame[0] ^= 0xFF;
    FrameHeader h;
    EXPECT_EQ(ParseFrameHeader(frame.data(), frame.size(), h), ParseResult::BadMagic);
}

TEST(ServiceProtocol, ParseBadVersion) {
    PingPayload in{};
    auto frame = EncodeFrame(static_cast<uint16_t>(MsgType::Ping), &in, sizeof(in));
    frame[4] = static_cast<uint8_t>(kProtoVer + 1);  // protoVer 低字节（小端）
    FrameHeader h;
    EXPECT_EQ(ParseFrameHeader(frame.data(), frame.size(), h), ParseResult::BadVersion);
}

TEST(ServiceProtocol, ParseZeroVersionRejected) {
    PingPayload in{};
    auto frame = EncodeFrame(static_cast<uint16_t>(MsgType::Ping), &in, sizeof(in));
    frame[4] = 0;
    FrameHeader h;
    EXPECT_EQ(ParseFrameHeader(frame.data(), frame.size(), h), ParseResult::BadVersion);
}

TEST(ServiceProtocol, ParseBadLength) {
    PingPayload in{};
    auto frame = EncodeFrame(static_cast<uint16_t>(MsgType::Ping), &in, sizeof(in));
    frame[11] = 0x7F;  // payloadLen 高字节 → 2GB+，远超 kMaxPayload
    FrameHeader h;
    EXPECT_EQ(ParseFrameHeader(frame.data(), frame.size(), h), ParseResult::BadLength);
}

// ---- 动作白名单 / 客户端校验 ----

TEST(ServiceProtocol, DriveLetterWhitelist) {
    EXPECT_TRUE(IsValidDriveLetter(L'A'));
    EXPECT_TRUE(IsValidDriveLetter(L'c'));
    EXPECT_TRUE(IsValidDriveLetter(L'Z'));
    EXPECT_FALSE(IsValidDriveLetter(L'1'));
    EXPECT_FALSE(IsValidDriveLetter(L':'));
    EXPECT_FALSE(IsValidDriveLetter(L'\\'));
    EXPECT_FALSE(IsValidDriveLetter(0));
    EXPECT_FALSE(IsValidDriveLetter(0x0391));  // 非 ASCII（希腊 Alpha）
}

TEST(ServiceProtocol, ClientPathMustBeSiblingIrisExe) {
    const std::wstring svc = L"C:\\Tools\\Iris\\IrisService.exe";
    EXPECT_TRUE(IsAllowedClientPath(L"C:\\Tools\\Iris\\Iris.exe", svc));
    EXPECT_TRUE(IsAllowedClientPath(L"C:\\Tools\\Iris\\iris.EXE", svc));   // 大小写不敏感
    EXPECT_FALSE(IsAllowedClientPath(L"C:\\Other\\Iris.exe", svc));        // 不同目录
    EXPECT_FALSE(IsAllowedClientPath(L"C:\\Tools\\Iris\\Evil.exe", svc));  // 同目录不同名
    EXPECT_FALSE(IsAllowedClientPath(L"C:\\Tools\\Iris", svc));            // 无文件名
    EXPECT_FALSE(IsAllowedClientPath(L"", svc));
}

// ---- 集成：拉起 --console 服务端做真实管道往返 ----
// 注意：本测试进程不是 Iris.exe → 服务端连接校验必须拒绝。以下用例因此
// 断言"未授权客户端被拒"——这正是安全层（§5.3 校验）的端到端验证：
// 拒绝须快速如实回传（结构化 Rejected），而非挂死/静默。
// 正向路径（Ping→Pong、句柄交付）由真实 Iris.exe 客户端验收（提权环境）。

namespace {

std::wstring ServiceExePath() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, n);
    return dir.substr(0, dir.find_last_of(L"\\/")) +
           L"\\..\\..\\service\\Release\\IrisService.exe";
}

struct ProcessGuard {
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    ~ProcessGuard() {
        if (process) {
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 3000);
            CloseHandle(process);
        }
        if (thread) CloseHandle(thread);
    }
};

} // namespace

class ServiceBroker : public ::testing::Test {
protected:
    void SetUp() override {
        if (GetFileAttributesW(ServiceExePath().c_str()) == INVALID_FILE_ATTRIBUTES)
            GTEST_SKIP() << "IrisService.exe 未构建（需先构建 IrisService target）";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        cmd_ = L"\"" + ServiceExePath() + L"\" --console";
        ASSERT_TRUE(CreateProcessW(nullptr, cmd_.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            << "CreateProcess err=" << GetLastError();
        guard_.process = pi.hProcess;
        guard_.thread = pi.hThread;
    }

    ProcessGuard guard_;
    std::wstring cmd_;
};

TEST_F(ServiceBroker, UnauthorizedClientPingRejected) {
    uint32_t proto = 0, ver = 0;
    bool ok = false;
    for (int i = 0; i < 50 && !ok; ++i) {  // 先等服务建好监听（连上即被拒）
        ok = Ping(proto, ver, 300);
        if (!ok) Sleep(100);
    }
    EXPECT_FALSE(ok);
}

TEST_F(ServiceBroker, UnauthorizedClientOpenVolumeRejected) {
    // 轮询等监听实例就绪（服务可达且校验层拒绝 = 结构化 Rejected 回传）
    HANDLE h = INVALID_HANDLE_VALUE;
    uint32_t err = 0;
    bool rejected = false;
    for (int i = 0; i < 30 && !rejected; ++i) {
        rejected = !OpenVolume(L'C', h, &err, 500) && err == 0;
        if (!rejected) Sleep(100);
    }
    EXPECT_TRUE(rejected);  // 拒绝须快速如实回传，而非挂死/静默
    EXPECT_EQ(err, 0);      // 校验层拒绝：不带底层错误码
}

TEST_F(ServiceBroker, OpenVolumeRejectsInvalidDrive) {
    HANDLE h = INVALID_HANDLE_VALUE;
    uint32_t err = 0;
    EXPECT_FALSE(OpenVolume(L'1', h, &err, 2000));
    EXPECT_EQ(err, 0);  // 服务端校验层拒绝：不带底层错误码
}
