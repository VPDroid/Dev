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

#ifndef SYSTEM_KEYMASTER_KEYMASTER_CONTEXT_H_
#define SYSTEM_KEYMASTER_KEYMASTER_CONTEXT_H_

#include <assert.h>

#include <hardware/keymaster_defs.h>
#include <keymaster/keymaster_enforcement.h>

namespace keymaster {

class AuthorizationSet;
class KeyFactory;
class OperationFactory;
struct KeymasterKeyBlob;

/**
 * KeymasterContext provides a singleton abstract interface that encapsulates various
 * environment-dependent elements of AndroidKeymaster.
 *
 * AndroidKeymaster runs in multiple contexts.  Primarily:
 *
 * - In a trusted execution environment (TEE) as a "secure hardware" implementation.  In this
 *   context keys are wrapped with an master key that never leaves the TEE, TEE-specific routines
 *   are used for random number generation, all AndroidKeymaster-enforced authorizations are
 *   considered hardware-enforced, and there's a bootloader-provided root of trust.
 *
 * - In the non-secure world as a software-only implementation.  In this context keys are not
 *   encrypted (though they are integrity-checked) because there is no place to securely store a
 *   key, OpenSSL is used for random number generation, no AndroidKeymaster-enforced authorizations
 *   are considered hardware enforced and the root of trust is a static string.
 *
 * - In the non-secure world as a hybrid implementation fronting a less-capable hardware
 *   implementation.  For example, a keymaster0 hardware implementation.  In this context keys are
 *   not encrypted by AndroidKeymaster, but some may be opaque blobs provided by the backing
 *   hardware, but blobs that lack the extended authorization lists of keymaster1.  In addition,
 *   keymaster0 lacks many features of keymaster1, including modes of operation related to the
 *   backing keymaster0 keys.  AndroidKeymaster must extend the blobs to add authorization lists,
 *   and must provide the missing operation mode implementations in software, which means that
 *   authorization lists are partially hardware-enforced (the bits that are enforced by the
 *   underlying keymaster0) and partially software-enforced (the rest). OpenSSL is used for number
 *   generation and the root of trust is a static string.
 *
 * More contexts are possible.
 */
class KeymasterContext {
  public:
    KeymasterContext() {}
    virtual ~KeymasterContext(){};

    virtual KeyFactory* GetKeyFactory(keymaster_algorithm_t algorithm) const = 0;
    virtual OperationFactory* GetOperationFactory(keymaster_algorithm_t algorithm,
                                                  keymaster_purpose_t purpose) const = 0;
    virtual keymaster_algorithm_t* GetSupportedAlgorithms(size_t* algorithms_count) const = 0;

    /**
     * CreateKeyBlob takes authorization sets and key material and produces a key blob and hardware
     * and software authorization lists ready to be returned to the AndroidKeymaster client
     * (Keystore, generally).  The blob is integrity-checked and may be encrypted, depending on the
     * needs of the context.
    *
     * This method is generally called only by KeyFactory subclassses.
     */
    virtual keymaster_error_t CreateKeyBlob(const AuthorizationSet& key_description,
                                            keymaster_key_origin_t origin,
                                            const KeymasterKeyBlob& key_material,
                                            KeymasterKeyBlob* blob, AuthorizationSet* hw_enforced,
                                            AuthorizationSet* sw_enforced) const = 0;

    /**
     * ParseKeyBlob takes a blob and extracts authorization sets and key material, returning an
     * error if the blob fails integrity checking or decryption.  Note that the returned key
     * material may itself be an opaque blob usable only by secure hardware (in the hybrid case).
     *
     * This method is called by AndroidKeymaster.
     */
    virtual keymaster_error_t ParseKeyBlob(const KeymasterKeyBlob& blob,
                                           const AuthorizationSet& additional_params,
                                           KeymasterKeyBlob* key_material,
                                           AuthorizationSet* hw_enforced,
                                           AuthorizationSet* sw_enforced) const = 0;

    /**
     * Take whatever environment-specific action is appropriate (if any) to delete the specified
     * key.
     */
    virtual keymaster_error_t DeleteKey(const KeymasterKeyBlob& /* blob */) const {
        return KM_ERROR_OK;
    }

    /**
     * Take whatever environment-specific action is appropriate to delete all keys.
     */
    virtual keymaster_error_t DeleteAllKeys() const { return KM_ERROR_OK; }

    /**
     * Adds entropy to the Cryptographic Pseudo Random Number Generator used to generate key
     * material, and other cryptographic protocol elements.  Note that if the underlying CPRNG
     * tracks the size of its entropy pool, it should not assume that the provided data contributes
     * any entropy, and it should also ensure that data provided through this interface cannot
     * "poison" the CPRNG outputs, making them predictable.
     */
    virtual keymaster_error_t AddRngEntropy(const uint8_t* buf, size_t length) const = 0;

    /**
     * Generates \p length random bytes, placing them in \p buf.
     */
    virtual keymaster_error_t GenerateRandom(uint8_t* buf, size_t length) const = 0;

    /**
     * Return the enforcement policy for this context, or null if no enforcement should be done.
     */
    virtual KeymasterEnforcement* enforcement_policy() = 0;

  private:
    // Uncopyable.
    KeymasterContext(const KeymasterContext&);
    void operator=(const KeymasterContext&);
};

}  // namespace keymaster

#endif  // SYSTEM_KEYMASTER_KEYMASTER_CONTEXT_H_
