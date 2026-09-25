/* SHA-1, for the launcher's check that a player's disc holds the executables
 * this port was built against (config/GGSPA4.toml). Written from FIPS 180-4;
 * SHA-1 is not used here for security, only to recognise a known file, which
 * is what the hashes in the config were made with. */
#include "sha1.h"

#include <string.h>

static uint32_t rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32u - n)); }

static void block(uint32_t h[5], const uint8_t* p)
{
    uint32_t w[80], a, b, c, d, e, t;
    unsigned i;
    for (i = 0; i < 16u; ++i)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (i = 16; i < 80u; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80u; ++i) {
        uint32_t f, k;
        if (i < 20u)      { f = (b & c) | (~b & d);           k = 0x5A827999u; }
        else if (i < 40u) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
        else if (i < 60u) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
        else              { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
        t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void mgs_sha1(const void* data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    const uint8_t* p = (const uint8_t*)data;
    uint8_t tail[128];
    size_t full = len / 64u, rest = len % 64u, n, i;
    uint64_t bits = (uint64_t)len * 8u;

    for (i = 0; i < full; ++i) block(h, p + 64u * i);
    memset(tail, 0, sizeof tail);
    memcpy(tail, p + 64u * full, rest);
    tail[rest] = 0x80u;
    n = rest < 56u ? 64u : 128u;
    for (i = 0; i < 8u; ++i) tail[n - 1u - i] = (uint8_t)(bits >> (8u * i));
    block(h, tail);
    if (n == 128u) block(h, tail + 64);
    for (i = 0; i < 5u; ++i) {
        out[4 * i]     = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)h[i];
    }
}

void mgs_sha1_hex(const void* data, size_t len, char out[41])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t d[20];
    unsigned i;
    mgs_sha1(data, len, d);
    for (i = 0; i < 20u; ++i) { out[2 * i] = hex[d[i] >> 4]; out[2 * i + 1] = hex[d[i] & 15u]; }
    out[40] = '\0';
}
