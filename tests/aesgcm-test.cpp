// aesgcm-test.cpp — проверка AES-256-GCM на официальных тест-векторах
// NIST SP 800-38D (Appendix B, Test Cases 13–16) + раундтрип + порча тега.
//
// Сборка (из корня репозитория):
//   g++ -fPIC tests/aesgcm-test.cpp src/net/AesGcm.cpp -I src -o /tmp/aes-test \
//       $(pkg-config --cflags --libs Qt6Core)

#include "net/AesGcm.h"
#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static QByteArray unhex(const char* s) {
    return QByteArray::fromHex(QByteArray(s));
}
static QByteArray mk(int n, char c) { return QByteArray(n, c); }

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ── NIST SP 800-38D, AES-256 (Test Cases 13–16) ──────────────────────────
    const QByteArray k0  = unhex("0000000000000000000000000000000000000000000000000000000000000000");
    const QByteArray kf  = unhex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    const QByteArray iv0 = unhex("000000000000000000000000");
    const QByteArray ivC = unhex("cafebabefacedbaddecaf888");

    printf("1) NIST SP 800-38D (AES-256)\n");
    {
        QByteArray out, tag;
        check(AesGcm::encrypt(k0, iv0, "", "", &out, &tag), "TC13: шифрование пустого сообщения");
        check(out.isEmpty() && tag == unhex("530f8afbc74536b9a963b4f1c4cb738b"),
              "TC13: тег 530f8afb…738b");
    }
    {
        QByteArray out, tag;
        AesGcm::encrypt(k0, iv0, mk(16, 0), "", &out, &tag);
        check(out == unhex("cea7403d4d606b6e074ec5d3baf39d18")
              && tag == unhex("d0d1c8a799996bf0265b98b5d48ab919"),
              "TC14: 16 нулевых байт → cea7403d… / тег d0d1c8a7…");
    }
    {
        const QByteArray p = unhex(
            "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
            "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255");
        QByteArray out, tag;
        AesGcm::encrypt(kf, ivC, p, "", &out, &tag);
        check(out == unhex(
            "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
            "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad")
              && tag == unhex("b094dac5d93471bdec1a502270e3cc6c"),
              "TC15: 64 байта без AAD → шифртекст+тег");
    }
    {
        const QByteArray p = unhex(
            "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
            "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
        const QByteArray aad = unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
        QByteArray out, tag;
        AesGcm::encrypt(kf, ivC, p, aad, &out, &tag);
        check(out == unhex(
            "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
            "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662")
              && tag == unhex("76fc6ece0f4e1768cddf8853bb2d551b"),
              "TC16: 60 байт + AAD 20 байт → шифртекст+тег");
    }

    // ── Раундтрип и отказ при порче ──────────────────────────────────────────
    printf("2) Раундтрип и целостность\n");
    {
        const QByteArray key = mk(32, 7), iv = mk(12, 9);
        const QByteArray plain = QByteArrayLiteral("Xipher stories media bytes 0123456789 \xff\x00");
        QByteArray out, tag, back;
        check(AesGcm::encrypt(key, iv, plain, QByteArrayLiteral("aad"), &out, &tag), "encrypt ок");
        check(AesGcm::decrypt(key, iv, out, QByteArrayLiteral("aad"), tag, &back)
              && back == plain, "decrypt → исходные байты");
        QByteArray badTag = tag; badTag[3] = char(badTag[3] ^ 1);
        check(!AesGcm::decrypt(key, iv, out, QByteArrayLiteral("aad"), badTag, &back),
              "испорченный тег — отказ");
        QByteArray badC = out; badC[0] = char(badC[0] ^ 1);
        check(!AesGcm::decrypt(key, iv, badC, QByteArrayLiteral("aad"), tag, &back),
              "испорченный шифртекст — отказ");
        check(!AesGcm::decrypt(key, iv, out, QByteArrayLiteral("other"), tag, &back),
              "чужой AAD — отказ");
        check(!AesGcm::encrypt(mk(16, 1), iv, plain, "", &out, &tag), "ключ не 32 байта — отказ");
    }

    printf("Итог: %s (%d ошибок)\n", failures ? "ЕСТЬ ОШИБКИ" : "ВСЁ ОК", failures);
    return failures ? 1 : 0;
}
