#include "net/SecretStore.h"
#include "net/Session.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cstring>

namespace {

constexpr int kNonceSize = 16;   // случайный nonce на каждую запись
constexpr int kTagSize    = 32;  // HMAC-SHA256
constexpr int kBlock      = 32;  // размер выхода HMAC-SHA256

QByteArray hmacSha256(const QByteArray& key, const QByteArray& msg) {
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
    mac.addData(msg);
    return mac.result();
}

// Сравнение тегов за постоянное время (без раннего выхода по байтам).
bool ctEqual(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

// Блок keystream: HMAC-SHA256(encKey, nonce ‖ counter_u32be) — PRF-CTR.
QByteArray keystreamBlock(const QByteArray& encKey, const QByteArray& nonce, quint32 counter) {
    QByteArray msg = nonce;
    msg.append(char(counter >> 24)); msg.append(char(counter >> 16));
    msg.append(char(counter >> 8));  msg.append(char(counter));
    return hmacSha256(encKey, msg);
}

QByteArray xorStream(const QByteArray& encKey, const QByteArray& nonce, const QByteArray& data) {
    QByteArray out(data.size(), 0);
    for (int off = 0; off < data.size(); off += kBlock) {
        const QByteArray ks = keystreamBlock(encKey, nonce, quint32(off / kBlock));
        const int n = qMin(kBlock, data.size() - off);
        for (int i = 0; i < n; ++i) out[off + i] = data[off + i] ^ ks[i];
    }
    return out;
}

QByteArray randomNonce() {
    quint32 buf[kNonceSize / 4];
    QRandomGenerator::system()->fillRange(buf, sizeof(buf) / sizeof(buf[0]));
    QByteArray nonce(kNonceSize, 0);
    memcpy(nonce.data(), buf, kNonceSize);
    return nonce;
}

} // namespace

SecretStore::SecretStore(QString ns) : ns_(std::move(ns)) {}

QString SecretStore::dirPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QLatin1Char('/') + ns_;
}

QString SecretStore::filePath(const QString& id) const {
    const QByteArray h = QCryptographicHash::hash(
        (Session::instance().userId + QLatin1Char('/') + id).toUtf8(),
        QCryptographicHash::Sha256).toHex();
    return dirPath() + QLatin1Char('/') + QString::fromLatin1(h.left(40)) + QStringLiteral(".xbs");
}

bool SecretStore::deriveKeys(const QString& ns, QByteArray* encKey, QByteArray* macKey) {
    const Session& s = Session::instance();
    if (s.token.isEmpty() || s.userId.isEmpty()) return false;
    // HKDF-Extract: PRK = HMAC-SHA256(salt, IKM=token); токен — высокоэнтропийный
    // секрет, медленный PBKDF2 не нужен.
    const QByteArray prk = hmacSha256(QByteArrayLiteral("xipher-desktop-secret-store-v1"),
                                      s.token.toUtf8());
    // HKDF-Expand: info включает ns (ключи хранилищ независимы) и userId
    // (файлы чужого аккаунта не пройдут проверку тега).
    const QByteArray uid = s.userId.toUtf8();
    const QByteArray nsB = ns.toUtf8();
    *encKey = hmacSha256(prk, QByteArrayLiteral("enc") + nsB + uid + char(1));
    *macKey = hmacSha256(prk, QByteArrayLiteral("mac") + nsB + uid + char(1));
    return true;
}

QByteArray SecretStore::load(const QString& id) const {
    QByteArray encKey, macKey;
    if (!deriveKeys(ns_, &encKey, &macKey)) return {};

    QFile f(filePath(id));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray blob = f.readAll();

    constexpr int kHeader = 4 + 4 + kNonceSize;   // magic + version + nonce
    if (blob.size() < kHeader + kTagSize) return {};
    if (blob.left(4) != QByteArrayLiteral("XBS1")) return {};

    const QByteArray nonce  = blob.mid(8, kNonceSize);
    const QByteArray ct     = blob.mid(kHeader, blob.size() - kHeader - kTagSize);
    const QByteArray tag    = blob.right(kTagSize);
    const QByteArray expect = hmacSha256(macKey, blob.left(blob.size() - kTagSize));
    if (!ctEqual(tag, expect)) return {};   // чужой аккаунт, битый или подменённый файл

    return xorStream(encKey, nonce, ct);
}

bool SecretStore::save(const QString& id, const QByteArray& bytes) {
    QByteArray encKey, macKey;
    if (!deriveKeys(ns_, &encKey, &macKey)) return false;

    // Каталог создаём только при записи (чтение без побочных эффектов).
    const QString dir = dirPath();
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QByteArray nonce = randomNonce();       // новый nonce — keystream не повторяется
    QByteArray out;
    out.append(QByteArrayLiteral("XBS1"));
    out.append(char(0)); out.append(char(0)); out.append(char(0)); out.append(char(1));
    out += nonce;
    out += xorStream(encKey, nonce, bytes);
    out += hmacSha256(macKey, out);               // тег покрывает заголовок и шифртекст

    // Атомарная запись: временный файл + rename — сбой не портит прошлые данные.
    QSaveFile f(filePath(id));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(out);
    return f.commit();
}

bool SecretStore::touch(const QString& id) {
    QFile f(filePath(id));
    if (!f.open(QIODevice::ReadWrite)) return false;
    return f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
}

void SecretStore::clearAll() const {
    QDir(dirPath()).removeRecursively();
}

qint64 SecretStore::totalBytes() const {
    qint64 total = 0;
    const auto infos = QDir(dirPath()).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : infos) total += fi.size();
    return total;
}
