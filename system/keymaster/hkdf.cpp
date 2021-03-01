/*
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hkdf.h"

#include <assert.h>

#include <new>

#include <keymaster/android_keymaster_utils.h>

#include "hmac.h"

namespace keymaster {

const size_t kSHA256HashLength = 32;

Rfc5869HmacSha256Kdf::Rfc5869HmacSha256Kdf(Buffer& secret, Buffer& salt, Buffer& info,
                                           size_t key_bytes_to_generate, keymaster_error_t* error) {
    Rfc5869HmacSha256Kdf(secret.peek_read(), secret.available_read(), salt.peek_read(),
                         salt.available_read(), info.peek_read(), info.available_read(),
                         key_bytes_to_generate, error);
}

Rfc5869HmacSha256Kdf::Rfc5869HmacSha256Kdf(const uint8_t* secret, size_t secret_len,
                                           const uint8_t* salt, size_t salt_len,
                                           const uint8_t* info, size_t info_len,
                                           size_t key_bytes_to_generate, keymaster_error_t* error) {
    // Step 1. Extract: PRK = HMAC-SHA256(actual_salt, secret)
    // https://tools.ietf.org/html/rfc5869#section-2.2
    HmacSha256 prk_hmac;
    bool result;
    if (salt) {
        result = prk_hmac.Init(salt, salt_len);
    } else {
        uint8_t zeros[kSHA256HashLength];
        // If salt is not given, HashLength zeros are used.
        memset(zeros, 0, sizeof(zeros));
        result = prk_hmac.Init(zeros, sizeof(zeros));
    }
    assert(result);
    // avoid the unused variable warning if asserts are disabled.
    (void)result;

    // |prk| is a pseudorandom key (of kSHA256HashLength octets).
    uint8_t prk[kSHA256HashLength];
    assert(sizeof(prk) == prk_hmac.DigestLength());
    result = prk_hmac.Sign(secret, secret_len, prk, sizeof(prk));
    assert(result);

    // Step 2. Expand: OUTPUT = HKDF-Expand(PRK, info)
    // https://tools.ietf.org/html/rfc5869#section-2.3
    const size_t n = (key_bytes_to_generate + kSHA256HashLength - 1) / kSHA256HashLength;
    assert(n < 256u);
    output_.reset(new (std::nothrow) uint8_t[n * kSHA256HashLength]);
    if (!output_.get()) {
        *error = KM_ERROR_MEMORY_ALLOCATION_FAILED;
        return;
    }

    uint8_t buf[kSHA256HashLength + info_len + 1];
    uint8_t digest[kSHA256HashLength];
    HmacSha256 hmac;
    result = hmac.Init(prk, sizeof(prk));
    assert(result);

    for (size_t i = 1; i <= n; i++) {
        size_t j = 0;
        if (i != 1) {
            memcpy(buf, digest, sizeof(digest));
            j = sizeof(digest);
        }
        memcpy(buf + j, info, info_len);
        j += info_len;
        buf[j++] = static_cast<uint8_t>(i);
        result = hmac.Sign(buf, j, digest, sizeof(digest));
        assert(result);
        memcpy(output_.get() + (i - 1) * sizeof(digest), digest, sizeof(digest));
    }

    if (key_bytes_to_generate) {
        secret_key_len_ = key_bytes_to_generate;
        secret_key_.reset(dup_buffer(output_.get(), key_bytes_to_generate));
        if (!secret_key_.get()) {
            *error = KM_ERROR_MEMORY_ALLOCATION_FAILED;
            return;
        }
    }
    *error = KM_ERROR_OK;
}

}  // namespace keymaster
