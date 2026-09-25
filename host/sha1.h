#ifndef MGS_SHA1_H
#define MGS_SHA1_H

#include <stddef.h>
#include <stdint.h>

void mgs_sha1(const void* data, size_t len, uint8_t out[20]);
void mgs_sha1_hex(const void* data, size_t len, char out[41]);

#endif
