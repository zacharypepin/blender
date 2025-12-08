#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif

#define HASH_SHA256_SIZE 32U

typedef struct
{
    uint8_t bytes[HASH_SHA256_SIZE];
} hash_sha256_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// hash_sha256_z: Compute SHA-256 hash of the given data using OpenSSL. Returns a hash_sha256_zt struct containing the hash bytes.
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C hash_sha256_zt hash_sha256_z(const void* data, size_t len);
