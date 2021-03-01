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

#ifndef SYSTEM_KEYMASTER_HKDF_H_
#define SYSTEM_KEYMASTER_HKDF_H_

#include <hardware/keymaster_defs.h>
#include <keymaster/serializable.h>

#include <UniquePtr.h>

namespace keymaster {

// Rfc5869HmacSha256Kdf implements the key derivation function specified in RFC 5869 (using
// SHA256) and outputs key material, as needed by ECIES.
// See https://tools.ietf.org/html/rfc5869 for details.
class Rfc5869HmacSha256Kdf {
  public:
    // |secret|: the input shared secret (or, from RFC 5869, the IKM).
    // |salt|: an (optional) public salt / non-secret random value. While
    // optional, callers are strongly recommended to provide a salt. There is no
    // added security value in making this larger than the SHA-256 block size of
    // 64 bytes.
    // |info|: an (optional) label to distinguish different uses of HKDF. It is
    // optional context and application specific information (can be a zero-length
    // string).
    // |key_bytes_to_generate|: the number of bytes of key material to generate.
    Rfc5869HmacSha256Kdf(Buffer& secret, Buffer& salt, Buffer& info, size_t key_bytes_to_generate,
                         keymaster_error_t* error);

    Rfc5869HmacSha256Kdf(const uint8_t* secret, size_t secret_len, const uint8_t* salt,
                         size_t salt_len, const uint8_t* info, size_t info_len,
                         size_t key_bytes_to_generate, keymaster_error_t* error);

    bool secret_key(Buffer* buf) const {
        return buf->Reinitialize(secret_key_.get(), secret_key_len_);
    };

  private:
    UniquePtr<uint8_t[]> output_;
    UniquePtr<uint8_t[]> secret_key_;
    size_t secret_key_len_;
};

}  // namespace keymaster

#endif  // SYSTEM_KEYMASTER_HKDF_H_
