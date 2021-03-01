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

#include <keymaster/soft_keymaster_device.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>

#include <algorithm>

#include <type_traits>

#include <openssl/x509.h>

#include <hardware/keymaster1.h>
#define LOG_TAG "SoftKeymasterDevice"
#include <cutils/log.h>

#include <keymaster/android_keymaster.h>
#include <keymaster/android_keymaster_messages.h>
#include <keymaster/authorization_set.h>
#include <keymaster/soft_keymaster_context.h>
#include <keymaster/soft_keymaster_logger.h>

#include "openssl_utils.h"

struct keystore_module soft_keymaster_device_module = {
    .common =
        {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = KEYMASTER_MODULE_API_VERSION_1_0,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = KEYSTORE_HARDWARE_MODULE_ID,
            .name = "OpenSSL-based SoftKeymaster HAL",
            .author = "The Android Open Source Project",
            .methods = NULL,
            .dso = 0,
            .reserved = {},
        },
};

namespace keymaster {

const size_t kOperationTableSize = 16;

typedef std::map<std::pair<keymaster_algorithm_t, keymaster_purpose_t>,
                 std::vector<keymaster_digest_t>> DigestMap;

template <typename T> std::vector<T> make_vector(const T* array, size_t len) {
    return std::vector<T>(array, array + len);
}

static keymaster_error_t add_digests(keymaster1_device_t* dev, keymaster_algorithm_t algorithm,
                                     keymaster_purpose_t purpose, DigestMap* map) {
    auto key = std::make_pair(algorithm, purpose);

    keymaster_digest_t* digests;
    size_t digests_length;
    keymaster_error_t error =
        dev->get_supported_digests(dev, algorithm, purpose, &digests, &digests_length);
    if (error != KM_ERROR_OK) {
        LOG_E("Error %d getting supported digests from keymaster1 device", error);
        return error;
    }
    std::unique_ptr<keymaster_digest_t, Malloc_Delete> digests_deleter(digests);

    (*map)[key] = make_vector(digests, digests_length);
    return KM_ERROR_OK;
}

static keymaster_error_t map_digests(keymaster1_device_t* dev, DigestMap* map) {
    map->clear();

    keymaster_algorithm_t sig_algorithms[] = {KM_ALGORITHM_RSA, KM_ALGORITHM_EC};
    keymaster_purpose_t sig_purposes[] = {KM_PURPOSE_SIGN, KM_PURPOSE_VERIFY};
    for (auto algorithm : sig_algorithms)
        for (auto purpose : sig_purposes) {
            keymaster_error_t error = add_digests(dev, algorithm, purpose, map);
            if (error != KM_ERROR_OK)
                return error;
        }

    keymaster_algorithm_t crypt_algorithms[] = {KM_ALGORITHM_RSA};
    keymaster_purpose_t crypt_purposes[] = {KM_PURPOSE_ENCRYPT, KM_PURPOSE_DECRYPT};
    for (auto algorithm : crypt_algorithms)
        for (auto purpose : crypt_purposes) {
            keymaster_error_t error = add_digests(dev, algorithm, purpose, map);
            if (error != KM_ERROR_OK)
                return error;
        }

    return KM_ERROR_OK;
}

SoftKeymasterDevice::SoftKeymasterDevice()
    : wrapped_km0_device_(nullptr), wrapped_km1_device_(nullptr),
      context_(new SoftKeymasterContext),
      impl_(new AndroidKeymaster(context_, kOperationTableSize)) {
    static_assert(std::is_standard_layout<SoftKeymasterDevice>::value,
                  "SoftKeymasterDevice must be standard layout");
    static_assert(offsetof(SoftKeymasterDevice, device_) == 0,
                  "device_ must be the first member of SoftKeymasterDevice");
    static_assert(offsetof(SoftKeymasterDevice, device_.common) == 0,
                  "common must be the first member of keymaster_device");
    LOG_I("Creating device", 0);
    LOG_D("Device address: %p", this);

    initialize_device_struct();
}

SoftKeymasterDevice::SoftKeymasterDevice(SoftKeymasterContext* context)
    : wrapped_km0_device_(nullptr), wrapped_km1_device_(nullptr), context_(context),
      impl_(new AndroidKeymaster(context_, kOperationTableSize)) {
    static_assert(std::is_standard_layout<SoftKeymasterDevice>::value,
                  "SoftKeymasterDevice must be standard layout");
    static_assert(offsetof(SoftKeymasterDevice, device_) == 0,
                  "device_ must be the first member of SoftKeymasterDevice");
    static_assert(offsetof(SoftKeymasterDevice, device_.common) == 0,
                  "common must be the first member of keymaster_device");
    LOG_I("Creating test device", 0);
    LOG_D("Device address: %p", this);

    initialize_device_struct();
}

keymaster_error_t SoftKeymasterDevice::SetHardwareDevice(keymaster0_device_t* keymaster0_device) {
    assert(keymaster0_device);
    LOG_D("Reinitializing SoftKeymasterDevice to use HW keymaster0", 0);

    if (!context_)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    keymaster_error_t error = context_->SetHardwareDevice(keymaster0_device);
    if (error != KM_ERROR_OK)
        return error;

    initialize_device_struct();

    module_name_ = device_.common.module->name;
    module_name_.append("(Wrapping ");
    module_name_.append(keymaster0_device->common.module->name);
    module_name_.append(")");

    updated_module_ = *device_.common.module;
    updated_module_.name = module_name_.c_str();

    device_.common.module = &updated_module_;

    wrapped_km0_device_ = keymaster0_device;
    wrapped_km1_device_ = nullptr;
    return KM_ERROR_OK;
}

keymaster_error_t SoftKeymasterDevice::SetHardwareDevice(keymaster1_device_t* keymaster1_device) {
    assert(keymaster1_device);
    LOG_D("Reinitializing SoftKeymasterDevice to use HW keymaster1", 0);

    if (!context_)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    keymaster_error_t error = map_digests(keymaster1_device, &km1_device_digests_);
    if (error != KM_ERROR_OK)
        return error;

    error = context_->SetHardwareDevice(keymaster1_device);
    if (error != KM_ERROR_OK)
        return error;

    initialize_device_struct();

    module_name_ = device_.common.module->name;
    module_name_.append(" (Wrapping ");
    module_name_.append(keymaster1_device->common.module->name);
    module_name_.append(")");

    updated_module_ = *device_.common.module;
    updated_module_.name = module_name_.c_str();

    device_.common.module = &updated_module_;

    wrapped_km0_device_ = nullptr;
    wrapped_km1_device_ = keymaster1_device;
    return KM_ERROR_OK;
}

bool SoftKeymasterDevice::Keymaster1DeviceIsGood() {
    std::vector<keymaster_digest_t> expected_rsa_digests = {
        KM_DIGEST_NONE,      KM_DIGEST_MD5,       KM_DIGEST_SHA1,     KM_DIGEST_SHA_2_224,
        KM_DIGEST_SHA_2_256, KM_DIGEST_SHA_2_384, KM_DIGEST_SHA_2_512};
    std::vector<keymaster_digest_t> expected_ec_digests = {
        KM_DIGEST_NONE,      KM_DIGEST_SHA1,      KM_DIGEST_SHA_2_224,
        KM_DIGEST_SHA_2_256, KM_DIGEST_SHA_2_384, KM_DIGEST_SHA_2_512};

    for (auto& entry : km1_device_digests_) {
        if (entry.first.first == KM_ALGORITHM_RSA)
            if (!std::is_permutation(entry.second.begin(), entry.second.end(),
                                     expected_rsa_digests.begin()))
                return false;
        if (entry.first.first == KM_ALGORITHM_EC)
            if (!std::is_permutation(entry.second.begin(), entry.second.end(),
                                     expected_ec_digests.begin()))
                return false;
    }
    return true;
}

void SoftKeymasterDevice::initialize_device_struct() {
    memset(&device_, 0, sizeof(device_));

    device_.common.tag = HARDWARE_DEVICE_TAG;
    device_.common.version = 1;
    device_.common.module = reinterpret_cast<hw_module_t*>(&soft_keymaster_device_module);
    device_.common.close = &close_device;

    device_.flags = KEYMASTER_BLOBS_ARE_STANDALONE | KEYMASTER_SUPPORTS_EC;

    // keymaster0 APIs
    device_.generate_keypair = nullptr;
    device_.import_keypair = nullptr;
    device_.get_keypair_public = nullptr;
    device_.delete_keypair = nullptr;
    device_.delete_all = nullptr;
    device_.sign_data = nullptr;
    device_.verify_data = nullptr;

    // keymaster1 APIs
    device_.get_supported_algorithms = get_supported_algorithms;
    device_.get_supported_block_modes = get_supported_block_modes;
    device_.get_supported_padding_modes = get_supported_padding_modes;
    device_.get_supported_digests = get_supported_digests;
    device_.get_supported_import_formats = get_supported_import_formats;
    device_.get_supported_export_formats = get_supported_export_formats;
    device_.add_rng_entropy = add_rng_entropy;
    device_.generate_key = generate_key;
    device_.get_key_characteristics = get_key_characteristics;
    device_.import_key = import_key;
    device_.export_key = export_key;
    device_.delete_key = delete_key;
    device_.delete_all_keys = delete_all_keys;
    device_.begin = begin;
    device_.update = update;
    device_.finish = finish;
    device_.abort = abort;

    device_.context = NULL;
}

const uint64_t HUNDRED_YEARS = 1000LL * 60 * 60 * 24 * 365 * 100;

hw_device_t* SoftKeymasterDevice::hw_device() {
    return &device_.common;
}

keymaster1_device_t* SoftKeymasterDevice::keymaster_device() {
    return &device_;
}

static keymaster_key_characteristics_t* BuildCharacteristics(const AuthorizationSet& hw_enforced,
                                                             const AuthorizationSet& sw_enforced) {
    keymaster_key_characteristics_t* characteristics =
        reinterpret_cast<keymaster_key_characteristics_t*>(
            malloc(sizeof(keymaster_key_characteristics_t)));
    if (characteristics) {
        hw_enforced.CopyToParamSet(&characteristics->hw_enforced);
        sw_enforced.CopyToParamSet(&characteristics->sw_enforced);
    }
    return characteristics;
}

template <typename RequestType>
static void AddClientAndAppData(const keymaster_blob_t* client_id, const keymaster_blob_t* app_data,
                                RequestType* request) {
    request->additional_params.Clear();
    if (client_id)
        request->additional_params.push_back(TAG_APPLICATION_ID, *client_id);
    if (app_data)
        request->additional_params.push_back(TAG_APPLICATION_DATA, *app_data);
}

static inline SoftKeymasterDevice* convert_device(const keymaster1_device_t* dev) {
    return reinterpret_cast<SoftKeymasterDevice*>(const_cast<keymaster1_device_t*>(dev));
}

/* static */
int SoftKeymasterDevice::close_device(hw_device_t* dev) {
    delete reinterpret_cast<SoftKeymasterDevice*>(dev);
    return 0;
}

/* static */
keymaster_error_t SoftKeymasterDevice::get_supported_algorithms(const keymaster1_device_t* dev,
                                                                keymaster_algorithm_t** algorithms,
                                                                size_t* algorithms_length) {
    if (!dev)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!algorithms || !algorithms_length)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->get_supported_algorithms(km1_dev, algorithms, algorithms_length);

    SupportedAlgorithmsRequest request;
    SupportedAlgorithmsResponse response;
    convert_device(dev)->impl_->SupportedAlgorithms(request, &response);
    if (response.error != KM_ERROR_OK) {
        LOG_E("get_supported_algorithms failed with %d", response.error);

        return response.error;
    }

    *algorithms_length = response.results_length;
    *algorithms =
        reinterpret_cast<keymaster_algorithm_t*>(malloc(*algorithms_length * sizeof(**algorithms)));
    if (!*algorithms)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    std::copy(response.results, response.results + response.results_length, *algorithms);
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::get_supported_block_modes(const keymaster1_device_t* dev,
                                                                 keymaster_algorithm_t algorithm,
                                                                 keymaster_purpose_t purpose,
                                                                 keymaster_block_mode_t** modes,
                                                                 size_t* modes_length) {
    if (!dev)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!modes || !modes_length)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->get_supported_block_modes(km1_dev, algorithm, purpose, modes, modes_length);

    SupportedBlockModesRequest request;
    request.algorithm = algorithm;
    request.purpose = purpose;
    SupportedBlockModesResponse response;
    convert_device(dev)->impl_->SupportedBlockModes(request, &response);

    if (response.error != KM_ERROR_OK) {
        LOG_E("get_supported_block_modes failed with %d", response.error);

        return response.error;
    }

    *modes_length = response.results_length;
    *modes = reinterpret_cast<keymaster_block_mode_t*>(malloc(*modes_length * sizeof(**modes)));
    if (!*modes)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    std::copy(response.results, response.results + response.results_length, *modes);
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::get_supported_padding_modes(const keymaster1_device_t* dev,
                                                                   keymaster_algorithm_t algorithm,
                                                                   keymaster_purpose_t purpose,
                                                                   keymaster_padding_t** modes,
                                                                   size_t* modes_length) {
    if (!dev)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!modes || !modes_length)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->get_supported_padding_modes(km1_dev, algorithm, purpose, modes,
                                                    modes_length);

    SupportedPaddingModesRequest request;
    request.algorithm = algorithm;
    request.purpose = purpose;
    SupportedPaddingModesResponse response;
    convert_device(dev)->impl_->SupportedPaddingModes(request, &response);

    if (response.error != KM_ERROR_OK) {
        LOG_E("get_supported_padding_modes failed with %d", response.error);
        return response.error;
    }

    *modes_length = response.results_length;
    *modes = reinterpret_cast<keymaster_padding_t*>(malloc(*modes_length * sizeof(**modes)));
    if (!*modes)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    std::copy(response.results, response.results + response.results_length, *modes);
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::get_supported_digests(const keymaster1_device_t* dev,
                                                             keymaster_algorithm_t algorithm,
                                                             keymaster_purpose_t purpose,
                                                             keymaster_digest_t** digests,
                                                             size_t* digests_length) {
    if (!dev)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!digests || !digests_length)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->get_supported_digests(km1_dev, algorithm, purpose, digests, digests_length);

    SupportedDigestsRequest request;
    request.algorithm = algorithm;
    request.purpose = purpose;
    SupportedDigestsResponse response;
    convert_device(dev)->impl_->SupportedDigests(request, &response);

    if (response.error != KM_ERROR_OK) {
        LOG_E("get_supported_digests failed with %d", response.error);
        return response.error;
    }

    *digests_length = response.results_length;
    *digests = reinterpret_cast<keymaster_digest_t*>(malloc(*digests_length * sizeof(**digests)));
    if (!*digests)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    std::copy(response.results, response.results + response.results_length, *digests);
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::get_supported_import_formats(
    const keymaster1_device_t* dev, keymaster_algorithm_t algorithm,
    keymaster_key_format_t** formats, size_t* formats_length) {
    if (!dev)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!formats || !formats_length)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->get_supported_import_formats(km1_dev, algorithm, formats, formats_length);

    SupportedImportFormatsRequest request;
    request.algorithm = algorithm;
    SupportedImportFormatsResponse response;
    convert_device(dev)->impl_->SupportedImportFormats(request, &response);

    if (response.error != KM_ERROR_OK) {
        LOG_E("get_supported_import_formats failed with %d", response.error);
        return response.error;
    }

    *formats_length = response.results_length;
    *formats =
        reinterpret_cast<keymaster_key_format_t*>(malloc(*formats_length * sizeof(**formats)));
    if (!*formats)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    std::copy(response.results, response.results + response.results_length, *formats);
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::get_supported_export_formats(
    const keymaster1_device_t* dev, keymaster_algorithm_t algorithm,
    keymaster_key_format_t** formats, size_t* formats_length) {
    if (!dev)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!formats || !formats_length)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->get_supported_export_formats(km1_dev, algorithm, formats, formats_length);

    SupportedExportFormatsRequest request;
    request.algorithm = algorithm;
    SupportedExportFormatsResponse response;
    convert_device(dev)->impl_->SupportedExportFormats(request, &response);

    if (response.error != KM_ERROR_OK) {
        LOG_E("get_supported_export_formats failed with %d", response.error);
        return response.error;
    }

    *formats_length = response.results_length;
    *formats =
        reinterpret_cast<keymaster_key_format_t*>(malloc(*formats_length * sizeof(**formats)));
    if (!*formats)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    std::copy(response.results, response.results + *formats_length, *formats);
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::add_rng_entropy(const keymaster1_device_t* dev,
                                                       const uint8_t* data, size_t data_length) {
    if (!dev)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->add_rng_entropy(km1_dev, data, data_length);

    AddEntropyRequest request;
    request.random_data.Reinitialize(data, data_length);
    AddEntropyResponse response;
    convert_device(dev)->impl_->AddRngEntropy(request, &response);
    if (response.error != KM_ERROR_OK)
        LOG_E("add_rng_entropy failed with %d", response.error);
    return response.error;
}

template <typename Collection, typename Value> bool contains(const Collection& c, const Value& v) {
    return std::find(c.begin(), c.end(), v) != c.end();
}

bool SoftKeymasterDevice::FindUnsupportedDigest(keymaster_algorithm_t algorithm,
                                                keymaster_purpose_t purpose,
                                                const AuthorizationSet& params,
                                                keymaster_digest_t* unsupported) const {
    assert(wrapped_km1_device_);

    auto supported_digests = km1_device_digests_.find(std::make_pair(algorithm, purpose));
    if (supported_digests == km1_device_digests_.end())
        // Invalid algorith/purpose pair (e.g. EC encrypt).  Let the error be handled by HW module.
        return false;

    for (auto& entry : params)
        if (entry.tag == TAG_DIGEST)
            if (!contains(supported_digests->second, entry.enumerated)) {
                LOG_I("Digest %d requested but not supported by module %s", entry.enumerated,
                      wrapped_km1_device_->common.module->name);
                *unsupported = static_cast<keymaster_digest_t>(entry.enumerated);
                return true;
            }
    return false;
}

bool SoftKeymasterDevice::RequiresSoftwareDigesting(keymaster_algorithm_t algorithm,
                                                    keymaster_purpose_t purpose,
                                                    const AuthorizationSet& params) const {
    assert(wrapped_km1_device_);
    if (!wrapped_km1_device_)
        return true;

    switch (algorithm) {
    case KM_ALGORITHM_AES:
    case KM_ALGORITHM_HMAC:
        LOG_D("Not performing software digesting for algorithm %d", algorithm);
        return false;
    case KM_ALGORITHM_RSA:
    case KM_ALGORITHM_EC:
        break;
    }

    keymaster_digest_t unsupported;
    if (!FindUnsupportedDigest(algorithm, purpose, params, &unsupported)) {
        LOG_D("Requested digest(s) supported for algorithm %d and purpose %d", algorithm, purpose);
        return false;
    }

    return true;
}

bool SoftKeymasterDevice::KeyRequiresSoftwareDigesting(
    const AuthorizationSet& key_description) const {
    assert(wrapped_km1_device_);
    if (!wrapped_km1_device_)
        return true;

    keymaster_algorithm_t algorithm;
    if (!key_description.GetTagValue(TAG_ALGORITHM, &algorithm)) {
        // The hardware module will return an error during keygen.
        return false;
    }

    for (auto& entry : key_description)
        if (entry.tag == TAG_PURPOSE) {
            keymaster_purpose_t purpose = static_cast<keymaster_purpose_t>(entry.enumerated);
            if (RequiresSoftwareDigesting(algorithm, purpose, key_description))
                return true;
        }

    return false;
}

/* static */
keymaster_error_t SoftKeymasterDevice::generate_key(
    const keymaster1_device_t* dev, const keymaster_key_param_set_t* params,
    keymaster_key_blob_t* key_blob, keymaster_key_characteristics_t** characteristics) {
    if (!dev || !params)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!key_blob)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    SoftKeymasterDevice* sk_dev = convert_device(dev);

    GenerateKeyRequest request;
    request.key_description.Reinitialize(*params);

    keymaster1_device_t* km1_dev = sk_dev->wrapped_km1_device_;
    if (km1_dev && !sk_dev->KeyRequiresSoftwareDigesting(request.key_description))
        return km1_dev->generate_key(km1_dev, params, key_blob, characteristics);

    GenerateKeyResponse response;
    sk_dev->impl_->GenerateKey(request, &response);
    if (response.error != KM_ERROR_OK)
        return response.error;

    key_blob->key_material_size = response.key_blob.key_material_size;
    uint8_t* tmp = reinterpret_cast<uint8_t*>(malloc(key_blob->key_material_size));
    if (!tmp)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    memcpy(tmp, response.key_blob.key_material, response.key_blob.key_material_size);
    key_blob->key_material = tmp;

    if (characteristics) {
        *characteristics = BuildCharacteristics(response.enforced, response.unenforced);
        if (!*characteristics)
            return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    }

    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::get_key_characteristics(
    const keymaster1_device_t* dev, const keymaster_key_blob_t* key_blob,
    const keymaster_blob_t* client_id, const keymaster_blob_t* app_data,
    keymaster_key_characteristics_t** characteristics) {
    if (!dev || !key_blob || !key_blob->key_material)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!characteristics)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->get_key_characteristics(km1_dev, key_blob, client_id, app_data,
                                                characteristics);

    GetKeyCharacteristicsRequest request;
    request.SetKeyMaterial(*key_blob);
    AddClientAndAppData(client_id, app_data, &request);

    GetKeyCharacteristicsResponse response;
    convert_device(dev)->impl_->GetKeyCharacteristics(request, &response);
    if (response.error != KM_ERROR_OK)
        return response.error;

    *characteristics = BuildCharacteristics(response.enforced, response.unenforced);
    if (!*characteristics)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::import_key(
    const keymaster1_device_t* dev, const keymaster_key_param_set_t* params,
    keymaster_key_format_t key_format, const keymaster_blob_t* key_data,
    keymaster_key_blob_t* key_blob, keymaster_key_characteristics_t** characteristics) {
    if (!params || !key_data)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!key_blob)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    SoftKeymasterDevice* sk_dev = convert_device(dev);

    ImportKeyRequest request;
    request.key_description.Reinitialize(*params);

    keymaster1_device_t* km1_dev = sk_dev->wrapped_km1_device_;
    if (km1_dev && !sk_dev->KeyRequiresSoftwareDigesting(request.key_description))
        return km1_dev->import_key(km1_dev, params, key_format, key_data, key_blob,
                                   characteristics);

    *characteristics = nullptr;

    request.key_format = key_format;
    request.SetKeyMaterial(key_data->data, key_data->data_length);

    ImportKeyResponse response;
    convert_device(dev)->impl_->ImportKey(request, &response);
    if (response.error != KM_ERROR_OK)
        return response.error;

    key_blob->key_material_size = response.key_blob.key_material_size;
    key_blob->key_material = reinterpret_cast<uint8_t*>(malloc(key_blob->key_material_size));
    if (!key_blob->key_material)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    memcpy(const_cast<uint8_t*>(key_blob->key_material), response.key_blob.key_material,
           response.key_blob.key_material_size);

    if (characteristics) {
        *characteristics = BuildCharacteristics(response.enforced, response.unenforced);
        if (!*characteristics)
            return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    }
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::export_key(const keymaster1_device_t* dev,
                                                  keymaster_key_format_t export_format,
                                                  const keymaster_key_blob_t* key_to_export,
                                                  const keymaster_blob_t* client_id,
                                                  const keymaster_blob_t* app_data,
                                                  keymaster_blob_t* export_data) {
    if (!key_to_export || !key_to_export->key_material)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!export_data)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev)
        return km1_dev->export_key(km1_dev, export_format, key_to_export, client_id, app_data,
                                   export_data);

    export_data->data = nullptr;
    export_data->data_length = 0;

    ExportKeyRequest request;
    request.key_format = export_format;
    request.SetKeyMaterial(*key_to_export);
    AddClientAndAppData(client_id, app_data, &request);

    ExportKeyResponse response;
    convert_device(dev)->impl_->ExportKey(request, &response);
    if (response.error != KM_ERROR_OK)
        return response.error;

    export_data->data_length = response.key_data_length;
    uint8_t* tmp = reinterpret_cast<uint8_t*>(malloc(export_data->data_length));
    if (!tmp)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    memcpy(tmp, response.key_data, export_data->data_length);
    export_data->data = tmp;
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::delete_key(const struct keymaster1_device* dev,
                                                  const keymaster_key_blob_t* key) {
    if (!dev || !key || !key->key_material)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev && km1_dev->delete_key)
        return km1_dev->delete_key(km1_dev, key);

    const keymaster0_device_t* km0_dev = convert_device(dev)->wrapped_km0_device_;
    if (km0_dev && km0_dev->delete_keypair)
        if (km0_dev->delete_keypair(km0_dev, key->key_material, key->key_material_size) < 0)
            return KM_ERROR_UNKNOWN_ERROR;

    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::delete_all_keys(const struct keymaster1_device* dev) {
    if (!dev)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev && km1_dev->delete_all_keys)
        return km1_dev->delete_all_keys(km1_dev);

    const keymaster0_device_t* km0_dev = convert_device(dev)->wrapped_km0_device_;
    if (km0_dev && km0_dev->delete_all)
        if (km0_dev->delete_all(km0_dev) < 0)
            return KM_ERROR_UNKNOWN_ERROR;

    return KM_ERROR_OK;
}

static bool FindAlgorithm(const keymaster_key_param_set_t& params,
                          keymaster_algorithm_t* algorithm) {
    for (size_t i = 0; i < params.length; ++i)
        if (params.params[i].tag == KM_TAG_ALGORITHM) {
            *algorithm = static_cast<keymaster_algorithm_t>(params.params[i].enumerated);
            return true;
        }
    return false;
}

static keymaster_error_t GetAlgorithm(const keymaster1_device_t* dev,
                                      const keymaster_key_blob_t& key,
                                      const AuthorizationSet& in_params,
                                      keymaster_algorithm_t* algorithm) {
    keymaster_blob_t client_id = {nullptr, 0};
    keymaster_blob_t app_data = {nullptr, 0};
    keymaster_blob_t* client_id_ptr = nullptr;
    keymaster_blob_t* app_data_ptr = nullptr;
    if (in_params.GetTagValue(TAG_APPLICATION_ID, &client_id))
        client_id_ptr = &client_id;
    if (in_params.GetTagValue(TAG_APPLICATION_DATA, &app_data))
        app_data_ptr = &app_data;

    keymaster_key_characteristics_t* characteristics;
    keymaster_error_t error =
        dev->get_key_characteristics(dev, &key, client_id_ptr, app_data_ptr, &characteristics);
    if (error != KM_ERROR_OK)
        return error;
    std::unique_ptr<keymaster_key_characteristics_t, Characteristics_Delete>
        characteristics_deleter(characteristics);

    if (FindAlgorithm(characteristics->hw_enforced, algorithm))
        return KM_ERROR_OK;

    if (FindAlgorithm(characteristics->sw_enforced, algorithm))
        return KM_ERROR_OK;

    return KM_ERROR_INVALID_KEY_BLOB;
}

/* static */
keymaster_error_t SoftKeymasterDevice::begin(const keymaster1_device_t* dev,
                                             keymaster_purpose_t purpose,
                                             const keymaster_key_blob_t* key,
                                             const keymaster_key_param_set_t* in_params,
                                             keymaster_key_param_set_t* out_params,
                                             keymaster_operation_handle_t* operation_handle) {
    if (!key || !key->key_material)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!operation_handle)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev) {
        AuthorizationSet in_params_set(*in_params);

        keymaster_algorithm_t algorithm = KM_ALGORITHM_AES;
        keymaster_error_t error = GetAlgorithm(km1_dev, *key, in_params_set, &algorithm);
        if (error != KM_ERROR_OK)
            return error;

        if (!convert_device(dev)->RequiresSoftwareDigesting(algorithm, purpose, in_params_set)) {
            LOG_D("Operation supported by %s, passing through to keymaster1 module",
                  km1_dev->common.module->name);
            return km1_dev->begin(km1_dev, purpose, key, in_params, out_params, operation_handle);
        }
        LOG_I("Doing software digesting for keymaster1 module %s", km1_dev->common.module->name);
    }

    if (out_params) {
        out_params->params = nullptr;
        out_params->length = 0;
    }

    BeginOperationRequest request;
    request.purpose = purpose;
    request.SetKeyMaterial(*key);
    request.additional_params.Reinitialize(*in_params);

    BeginOperationResponse response;
    convert_device(dev)->impl_->BeginOperation(request, &response);
    if (response.error != KM_ERROR_OK)
        return response.error;

    if (response.output_params.size() > 0) {
        if (out_params)
            response.output_params.CopyToParamSet(out_params);
        else
            return KM_ERROR_OUTPUT_PARAMETER_NULL;
    }

    *operation_handle = response.op_handle;
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::update(const keymaster1_device_t* dev,
                                              keymaster_operation_handle_t operation_handle,
                                              const keymaster_key_param_set_t* in_params,
                                              const keymaster_blob_t* input, size_t* input_consumed,
                                              keymaster_key_param_set_t* out_params,
                                              keymaster_blob_t* output) {
    if (!input)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;

    if (!input_consumed)
        return KM_ERROR_OUTPUT_PARAMETER_NULL;

    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev && !convert_device(dev)->impl_->has_operation(operation_handle)) {
        // This operation is being handled by km1_dev (or doesn't exist).  Pass it through to
        // km1_dev.  Otherwise, we'll use the software AndroidKeymaster, which may delegate to
        // km1_dev after doing necessary digesting.
        return km1_dev->update(km1_dev, operation_handle, in_params, input, input_consumed,
                               out_params, output);
    }

    if (out_params) {
        out_params->params = nullptr;
        out_params->length = 0;
    }
    if (output) {
        output->data = nullptr;
        output->data_length = 0;
    }

    UpdateOperationRequest request;
    request.op_handle = operation_handle;
    if (input)
        request.input.Reinitialize(input->data, input->data_length);
    if (in_params)
        request.additional_params.Reinitialize(*in_params);

    UpdateOperationResponse response;
    convert_device(dev)->impl_->UpdateOperation(request, &response);
    if (response.error != KM_ERROR_OK)
        return response.error;

    if (response.output_params.size() > 0) {
        if (out_params)
            response.output_params.CopyToParamSet(out_params);
        else
            return KM_ERROR_OUTPUT_PARAMETER_NULL;
    }

    *input_consumed = response.input_consumed;
    if (output) {
        output->data_length = response.output.available_read();
        uint8_t* tmp = reinterpret_cast<uint8_t*>(malloc(output->data_length));
        if (!tmp)
            return KM_ERROR_MEMORY_ALLOCATION_FAILED;
        memcpy(tmp, response.output.peek_read(), output->data_length);
        output->data = tmp;
    } else if (response.output.available_read() > 0) {
        return KM_ERROR_OUTPUT_PARAMETER_NULL;
    }
    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::finish(const keymaster1_device_t* dev,
                                              keymaster_operation_handle_t operation_handle,
                                              const keymaster_key_param_set_t* params,
                                              const keymaster_blob_t* signature,
                                              keymaster_key_param_set_t* out_params,
                                              keymaster_blob_t* output) {
    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev && !convert_device(dev)->impl_->has_operation(operation_handle)) {
        // This operation is being handled by km1_dev (or doesn't exist).  Pass it through to
        // km1_dev.  Otherwise, we'll use the software AndroidKeymaster, which may delegate to
        // km1_dev after doing necessary digesting.
        return km1_dev->finish(km1_dev, operation_handle, params, signature, out_params, output);
    }

    if (out_params) {
        out_params->params = nullptr;
        out_params->length = 0;
    }

    if (output) {
        output->data = nullptr;
        output->data_length = 0;
    }

    FinishOperationRequest request;
    request.op_handle = operation_handle;
    if (signature && signature->data_length > 0)
        request.signature.Reinitialize(signature->data, signature->data_length);
    request.additional_params.Reinitialize(*params);

    FinishOperationResponse response;
    convert_device(dev)->impl_->FinishOperation(request, &response);
    if (response.error != KM_ERROR_OK)
        return response.error;

    if (response.output_params.size() > 0) {
        if (out_params)
            response.output_params.CopyToParamSet(out_params);
        else
            return KM_ERROR_OUTPUT_PARAMETER_NULL;
    }
    if (output) {
        output->data_length = response.output.available_read();
        uint8_t* tmp = reinterpret_cast<uint8_t*>(malloc(output->data_length));
        if (!tmp)
            return KM_ERROR_MEMORY_ALLOCATION_FAILED;
        memcpy(tmp, response.output.peek_read(), output->data_length);
        output->data = tmp;
    } else if (response.output.available_read() > 0) {
        return KM_ERROR_OUTPUT_PARAMETER_NULL;
    }

    return KM_ERROR_OK;
}

/* static */
keymaster_error_t SoftKeymasterDevice::abort(const keymaster1_device_t* dev,
                                             keymaster_operation_handle_t operation_handle) {
    const keymaster1_device_t* km1_dev = convert_device(dev)->wrapped_km1_device_;
    if (km1_dev && !convert_device(dev)->impl_->has_operation(operation_handle)) {
        // This operation is being handled by km1_dev (or doesn't exist).  Pass it through to
        // km1_dev.  Otherwise, we'll use the software AndroidKeymaster, which may delegate to
        // km1_dev.
        return km1_dev->abort(km1_dev, operation_handle);
    }

    AbortOperationRequest request;
    request.op_handle = operation_handle;
    AbortOperationResponse response;
    convert_device(dev)->impl_->AbortOperation(request, &response);
    return response.error;
}

/* static */
void SoftKeymasterDevice::StoreDefaultNewKeyParams(keymaster_algorithm_t algorithm,
                                                   AuthorizationSet* auth_set) {
    auth_set->push_back(TAG_PURPOSE, KM_PURPOSE_SIGN);
    auth_set->push_back(TAG_PURPOSE, KM_PURPOSE_VERIFY);
    auth_set->push_back(TAG_ALL_USERS);
    auth_set->push_back(TAG_NO_AUTH_REQUIRED);

    // All digests.
    auth_set->push_back(TAG_DIGEST, KM_DIGEST_NONE);
    auth_set->push_back(TAG_DIGEST, KM_DIGEST_MD5);
    auth_set->push_back(TAG_DIGEST, KM_DIGEST_SHA1);
    auth_set->push_back(TAG_DIGEST, KM_DIGEST_SHA_2_224);
    auth_set->push_back(TAG_DIGEST, KM_DIGEST_SHA_2_256);
    auth_set->push_back(TAG_DIGEST, KM_DIGEST_SHA_2_384);
    auth_set->push_back(TAG_DIGEST, KM_DIGEST_SHA_2_512);

    if (algorithm == KM_ALGORITHM_RSA) {
        auth_set->push_back(TAG_PURPOSE, KM_PURPOSE_ENCRYPT);
        auth_set->push_back(TAG_PURPOSE, KM_PURPOSE_DECRYPT);
        auth_set->push_back(TAG_PADDING, KM_PAD_NONE);
        auth_set->push_back(TAG_PADDING, KM_PAD_RSA_PKCS1_1_5_SIGN);
        auth_set->push_back(TAG_PADDING, KM_PAD_RSA_PKCS1_1_5_ENCRYPT);
        auth_set->push_back(TAG_PADDING, KM_PAD_RSA_PSS);
        auth_set->push_back(TAG_PADDING, KM_PAD_RSA_OAEP);
    }
}

}  // namespace keymaster
