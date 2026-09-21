#include "net/AesGcm.h"

#include <QHash>

// Компактная реализация AES-256 (только шифрование — GCM не использует дешифровку
// AES для данных) + GCM поверх неё (GHASH через таблицу умножений в GF(2^128)).
// Объём кода — примерно как один файл Qt-проекта; внешний криптомодуль ради
// двух вызовов не тянули.

namespace {

// ── AES-256: SBox, таблицы расширенного ключа ───────────────────────────────
const unsigned char kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

using Block = unsigned char[16];

unsigned char xtime(unsigned char x) {
    return static_cast<unsigned char>((x << 1) ^ ((x >> 7) * 0x1b));
}

void expandKey256(const unsigned char* key, unsigned char roundKeys[15][16]) {
    unsigned char w[60][4];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) w[i][j] = key[4 * i + j];
    unsigned char rcon = 1;
    for (int i = 8; i < 60; ++i) {
        unsigned char t[4];
        for (int j = 0; j < 4; ++j) t[j] = w[i - 1][j];
        if (i % 8 == 0) {
            const unsigned char tmp = t[0];
            t[0] = kSbox[t[1]] ^ rcon; t[1] = kSbox[t[2]];
            t[2] = kSbox[t[3]];        t[3] = kSbox[tmp];
            rcon = xtime(rcon);
        } else if (i % 8 == 4) {
            for (int j = 0; j < 4; ++j) t[j] = kSbox[t[j]];
        }
        for (int j = 0; j < 4; ++j) w[i][j] = w[i - 8][j] ^ t[j];
    }
    for (int r = 0; r < 15; ++r)
        for (int c = 0; c < 4; ++c)
            for (int j = 0; j < 4; ++j)
                roundKeys[r][4 * c + j] = w[4 * r + c][j];
}

void addRoundKey(Block s, const unsigned char* rk) {
    for (int i = 0; i < 16; ++i) s[i] ^= rk[i];
}
void subBytes(Block s)      { for (int i = 0; i < 16; ++i) s[i] = kSbox[s[i]]; }
void shiftRows(Block s) {
    unsigned char t;
    t = s[1];  s[1]  = s[5];  s[5]  = s[9];  s[9]  = s[13]; s[13] = t;
    t = s[2];  s[2]  = s[10]; s[10] = t;     t = s[6];  s[6]  = s[14]; s[14] = t;
    // Строка 3 — циклический сдвиг влево на 3: (a,b,c,d) → (d,a,b,c).
    t = s[15]; s[15] = s[11]; s[11] = s[7];  s[7]  = s[3];  s[3]  = t;
}
void mixColumns(Block s) {
    for (int c = 0; c < 4; ++c) {
        const unsigned char a0 = s[4*c], a1 = s[4*c+1], a2 = s[4*c+2], a3 = s[4*c+3];
        s[4*c]   = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3;
        s[4*c+1] = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3;
        s[4*c+2] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3);
        s[4*c+3] = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3);
    }
}

void aes256EncryptBlock(const unsigned char roundKeys[15][16], const Block in, Block out) {
    for (int i = 0; i < 16; ++i) out[i] = in[i];
    addRoundKey(out, roundKeys[0]);
    for (int r = 1; r < 14; ++r) {
        subBytes(out); shiftRows(out); mixColumns(out); addRoundKey(out, roundKeys[r]);
    }
    subBytes(out); shiftRows(out); addRoundKey(out, roundKeys[14]);
}

// ── GHASH: умножение в GF(2^128) по GCM-полиному (таблица 4-битных шагов) ────
void gfMult(const unsigned char X[16], const unsigned char H[16], unsigned char out[16]) {
    unsigned char Z[16] = {0}, V[16];
    for (int i = 0; i < 16; ++i) V[i] = H[i];
    for (int i = 0; i < 128; ++i) {
        const int byteIdx = i / 8, bitIdx = 7 - (i % 8);
        if ((X[byteIdx] >> bitIdx) & 1)
            for (int j = 0; j < 16; ++j) Z[j] ^= V[j];
        const bool lsb = V[15] & 1;
        for (int j = 15; j > 0; --j) V[j] = (V[j] >> 1) | static_cast<unsigned char>((V[j-1] & 1) << 7);
        V[0] >>= 1;
        if (lsb) V[0] ^= 0xe1;
    }
    for (int i = 0; i < 16; ++i) out[i] = Z[i];
}

// GHASH над данными, добитыми нулями до кратности 16 (как требует GCM).
static void ghashPadded(const unsigned char H[16], unsigned char Y[16],
                        const QByteArray& data) {
    int off = 0;
    while (off < data.size()) {
        const int n = qMin(16, data.size() - off);
        // Блок = Y XOR (data ‖ нули до 16): хвост частичного блока — Y, не нули.
        unsigned char block[16];
        for (int i = 0; i < 16; ++i) block[i] = Y[i];
        for (int i = 0; i < n; ++i)
            block[i] ^= static_cast<unsigned char>(data[off + i]);
        gfMult(block, H, Y);
        off += 16;
    }
}

} // namespace

namespace AesGcm {

struct Ctx {
    unsigned char roundKeys[15][16];
    unsigned char H[16];
    unsigned char J0[16];
};

static void gcmInit(Ctx& c, const QByteArray& key, const QByteArray& iv) {
    expandKey256(reinterpret_cast<const unsigned char*>(key.constData()), c.roundKeys);
    Block zeros = {0}, hBlock;
    aes256EncryptBlock(c.roundKeys, zeros, hBlock);
    for (int i = 0; i < 16; ++i) c.H[i] = hBlock[i];
    // J0 = IV ‖ 0^31 ‖ 1 (при IV 96 бит — стандартный случай).
    for (int i = 0; i < 12; ++i) c.J0[i] = static_cast<unsigned char>(iv[i]);
    c.J0[12] = c.J0[13] = c.J0[14] = 0;
    c.J0[15] = 1;
}

static void gcmCrypt(const Ctx& c, const QByteArray& input, QByteArray* out) {
    out->resize(input.size());
    unsigned char counter[16];
    for (int i = 0; i < 16; ++i) counter[i] = c.J0[i];
    unsigned char ks[16];
    for (int off = 0; off < input.size(); off += 16) {
        // инкремент счётчика (32-битный, big-endian, младшие 4 байта)
        for (int i = 15; i >= 12; --i) {
            if (++counter[i] != 0) break;
        }
        aes256EncryptBlock(c.roundKeys, counter, ks);
        const int n = qMin(16, input.size() - off);
        for (int i = 0; i < n; ++i)
            (*out)[off + i] = static_cast<char>(input[off + i] ^ ks[i]);
    }
}

static void gcmTag(const Ctx& c, const QByteArray& aad, const QByteArray& cipher,
                   unsigned char tag[16]) {
    unsigned char Y[16] = {0};
    // GHASH(pad(A) ‖ pad(C) ‖ lenA‖lenC в битах)
    if (aad.size() > 0) ghashPadded(c.H, Y, aad);
    if (cipher.size() > 0) ghashPadded(c.H, Y, cipher);
    unsigned char lenBlock[16] = {0};
    const quint64 aBits = quint64(aad.size()) * 8, cBits = quint64(cipher.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        lenBlock[i]    = static_cast<unsigned char>(aBits >> (56 - 8 * i));
        lenBlock[8 + i]= static_cast<unsigned char>(cBits >> (56 - 8 * i));
    }
    unsigned char yLen[16];
    for (int i = 0; i < 16; ++i) yLen[i] = Y[i] ^ lenBlock[i];
    // финал: GHASH → E_K(J0) XOR
    unsigned char s[16];
    gfMult(yLen, c.H, s);
    unsigned char eJ0[16];
    aes256EncryptBlock(c.roundKeys, c.J0, eJ0);
    for (int i = 0; i < 16; ++i) tag[i] = s[i] ^ eJ0[i];
}

bool encrypt(const QByteArray& key, const QByteArray& iv,
             const QByteArray& plain, const QByteArray& aad,
             QByteArray* out, QByteArray* tag) {
    if (key.size() != 32 || iv.size() != 12) return false;
    Ctx c;
    gcmInit(c, key, iv);
    gcmCrypt(c, plain, out);
    unsigned char t[16];
    gcmTag(c, aad, *out, t);
    tag->resize(16);
    for (int i = 0; i < 16; ++i) (*tag)[i] = static_cast<char>(t[i]);
    return true;
}

bool decrypt(const QByteArray& key, const QByteArray& iv,
             const QByteArray& cipher, const QByteArray& aad,
             const QByteArray& tag, QByteArray* out) {
    if (key.size() != 32 || iv.size() != 12 || tag.size() != 16) return false;
    Ctx c;
    gcmInit(c, key, iv);
    unsigned char expect[16];
    gcmTag(c, aad, cipher, expect);
    // постоянное время
    unsigned char diff = 0;
    for (int i = 0; i < 16; ++i)
        diff |= static_cast<unsigned char>(tag[i]) ^ expect[i];
    if (diff != 0) return false;
    gcmCrypt(c, cipher, out);
    return true;
}

} // namespace AesGcm
