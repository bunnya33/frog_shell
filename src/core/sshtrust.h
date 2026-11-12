#pragma once

#include "profile.h"
#include <libssh2.h>

namespace rs::sshtrust {
// Inline so the existing core target needs no extra source registration. The
// function-local static is one mutex across translation units (C++ ODR).
inline QMutex& mutex() { static QMutex value; return value; }
struct Result {
    enum Status { Trusted, Unknown, Changed, Error } status = Error;
    QString fingerprint;
    QString reason;
};
inline Result check(LIBSSH2_SESSION* ssh, const Profile& profile, bool accept = false) {
    size_t length = 0;
    int type = 0;
    const char* key = libssh2_session_hostkey(ssh, &length, &type);
    Result result;
    auto fail = [&](const QString& reason) {
        result.status = Result::Error;
        result.reason = reason;
        return result;
    };
    if (!key || !length) return fail("无法读取服务器主机密钥");
    result.fingerprint = "SHA256:" + QString::fromLatin1(
        QCryptographicHash::hash(QByteArray(key, qsizetype(length)), QCryptographicHash::Sha256)
            .toBase64(QByteArray::OmitTrailingEquals));
    int mask = LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
    switch (type) {
    case LIBSSH2_HOSTKEY_TYPE_RSA: mask = LIBSSH2_KNOWNHOST_KEY_SSHRSA; break;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: mask = LIBSSH2_KNOWNHOST_KEY_ECDSA_256; break;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: mask = LIBSSH2_KNOWNHOST_KEY_ECDSA_384; break;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: mask = LIBSSH2_KNOWNHOST_KEY_ECDSA_521; break;
    case LIBSSH2_HOSTKEY_TYPE_ED25519: mask = LIBSSH2_KNOWNHOST_KEY_ED25519; break;
    default: return fail("不支持的服务器主机密钥类型");
    }
    QMutexLocker lock(&mutex());
    const QString path = dataPath() + "/known_hosts";
    QLockFile processLock(path + ".lock");
    if (accept && !processLock.tryLock(0)) return fail("known_hosts 正被其他进程使用，无法保存");
    QFile input(path);
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly)) return fail("无法读取 known_hosts：" + input.errorString());
        if (input.size() > 4 * 1024 * 1024) return fail("known_hosts 超过 4 MiB 大小限制");
    }
    auto known = libssh2_knownhost_init(ssh);
    if (!known) return fail("无法创建 known_hosts 校验器");
    const auto cleanup = qScopeGuard([&] { libssh2_knownhost_free(known); });
    qint64 readBytes = 0;
    int lineNumber = 0;
    while (input.isOpen() && !input.atEnd()) {
        auto line = input.readLine(16385);
        ++lineNumber;
        readBytes += line.size();
        if (input.error() != QFileDevice::NoError) return fail("读取 known_hosts 失败：" + input.errorString());
        if (readBytes > 4 * 1024 * 1024) return fail("known_hosts 超过 4 MiB 大小限制");
        if (line.size() > 16384) return fail(QString("known_hosts 第 %1 行超过 16 KiB 长度限制").arg(lineNumber));
        // QFile binary reads retain CR. libssh2 only removes LF and otherwise
        // compares the stored base64 text verbatim. Normalize terminators only;
        // a bare or embedded CR/NUL must never be trimmed into a trusted record.
        if (line.endsWith('\n')) {
            line.chop(1);
            if (line.endsWith('\r')) line.chop(1);
        }
        for (const unsigned char ch : line) {
            if ((ch < 0x20 && ch != '\t') || ch == 0x7f)
                return fail(QString("known_hosts 第 %1 行含非法控制字符").arg(lineNumber));
        }
        qsizetype first = 0;
        while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) ++first;
        if (first == line.size() || line[first] == '#') continue;
        if (libssh2_knownhost_readline(known, line.constData(), size_t(line.size()),
                                     LIBSSH2_KNOWNHOST_FILE_OPENSSH) < 0)
            return fail(QString("known_hosts 第 %1 行格式无效").arg(lineNumber));
    }
    if (input.isOpen() && input.error() != QFileDevice::NoError)
        return fail("读取 known_hosts 失败：" + input.errorString());
    const int flags = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | mask;
    const auto host = profile.host.toUtf8();
    int rc = libssh2_knownhost_checkp(known, host.constData(), profile.port, key, length, flags, nullptr);
    if (rc == LIBSSH2_KNOWNHOST_CHECK_MATCH) { result.status = Result::Trusted; return result; }
    if (rc == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) { result.status = Result::Changed; return result; }
    if (rc != LIBSSH2_KNOWNHOST_CHECK_NOTFOUND) return fail("known_hosts 主机密钥校验器返回错误");
    result.status = Result::Unknown;
    if (!accept) return result;
    // A fresh read/check under the same lock also protects confirmation races.
    auto entry = (profile.port == 22 ? profile.host : QString("[%1]:%2").arg(profile.host).arg(profile.port)).toUtf8();
    if (libssh2_knownhost_addc(known, entry.constData(), nullptr, key, length, nullptr, 0, flags, nullptr)) {
        return fail("无法向 known_hosts 添加主机密钥");
    }
    input.close();
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return fail("无法保存 known_hosts：" + output.errorString());
    libssh2_knownhost* previous = nullptr;
    libssh2_knownhost* current = nullptr;
    char buffer[16384];
    qint64 total = 0;
    while (libssh2_knownhost_get(known, &current, previous) == 0) {
        size_t written = 0;
        if (libssh2_knownhost_writeline(known, current, buffer, sizeof(buffer), &written,
                                      LIBSSH2_KNOWNHOST_FILE_OPENSSH) ||
            (total += qint64(written)) > 4 * 1024 * 1024 ||
            output.write(buffer, qint64(written)) != qint64(written)) {
            return fail("无法完整写入 known_hosts，原文件保持不变");
        }
        previous = current;
    }
    if (!output.commit()) return fail("无法原子保存 known_hosts：" + output.errorString());
    result.status = Result::Trusted;
    return result;
}
} // namespace rs::sshtrust
