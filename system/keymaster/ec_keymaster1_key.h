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

#ifndef SYSTEM_KEYMASTER_EC_KEYMASTER1_KEY_H_
#define SYSTEM_KEYMASTER_EC_KEYMASTER1_KEY_H_

#include <openssl/ecdsa.h>

#include <hardware/keymaster1.h>
#include <keymaster/android_keymaster_utils.h>
#include <keymaster/logger.h>
#include <keymaster/ec_key_factory.h>
#include <keymaster/soft_keymaster_context.h>

#include "ec_key.h"
#include "keymaster1_engine.h"

namespace keymaster {

class SoftKeymasterContext;

/**
 * EcdsaKeymaster1KeyFactory is a KeyFactory that creates and loads keys which are actually backed
 * by a hardware keymaster1 module, but which does not support all keymaster1 digests.  During
 * generation or import any unsupported digests in the key description are silently replaced with
 * KM_DIGEST_NONE.
 */
class EcdsaKeymaster1KeyFactory : public EcKeyFactory {
  public:
    EcdsaKeymaster1KeyFactory(const SoftKeymasterContext* context, const Keymaster1Engine* engine);

    keymaster_error_t GenerateKey(const AuthorizationSet& key_description,
                                  KeymasterKeyBlob* key_blob, AuthorizationSet* hw_enforced,
                                  AuthorizationSet* sw_enforced) const override;

    keymaster_error_t ImportKey(const AuthorizationSet& key_description,
                                keymaster_key_format_t input_key_material_format,
                                const KeymasterKeyBlob& input_key_material,
                                KeymasterKeyBlob* output_key_blob, AuthorizationSet* hw_enforced,
                                AuthorizationSet* sw_enforced) const override;

    keymaster_error_t LoadKey(const KeymasterKeyBlob& key_material,
                              const AuthorizationSet& additional_params,
                              const AuthorizationSet& hw_enforced,
                              const AuthorizationSet& sw_enforced,
                              UniquePtr<Key>* key) const override;

    OperationFactory* GetOperationFactory(keymaster_purpose_t purpose) const override;

  private:
    const Keymaster1Engine* engine_;

    std::unique_ptr<OperationFactory> sign_factory_;
    std::unique_ptr<OperationFactory> verify_factory_;
};

class EcdsaKeymaster1Key : public EcKey {
  public:
    EcdsaKeymaster1Key(EC_KEY* ecdsa_key, const AuthorizationSet& hw_enforced,
                       const AuthorizationSet& sw_enforced, const Keymaster1Engine* engine,
                       keymaster_error_t* error);

  private:
    const Keymaster1Engine* engine_;
};

}  // namespace keymaster

#endif  // SYSTEM_KEYMASTER_ECDSA_KEYMASTER1_KEY_H_
