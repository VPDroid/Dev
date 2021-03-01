/*
 * Copyright 2014 The Android Open Source Project
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

#ifndef SYSTEM_KEYMASTER_ANDROID_KEYMASTER_MESSAGES_H_
#define SYSTEM_KEYMASTER_ANDROID_KEYMASTER_MESSAGES_H_

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <keymaster/authorization_set.h>
#include <keymaster/android_keymaster_utils.h>

namespace keymaster {

// Commands
enum AndroidKeymasterCommand {
    GENERATE_KEY = 0,
    BEGIN_OPERATION = 1,
    UPDATE_OPERATION = 2,
    FINISH_OPERATION = 3,
    ABORT_OPERATION = 4,
    IMPORT_KEY = 5,
    EXPORT_KEY = 6,
    GET_VERSION = 7,
    ADD_RNG_ENTROPY = 8,
    GET_SUPPORTED_ALGORITHMS = 9,
    GET_SUPPORTED_BLOCK_MODES = 10,
    GET_SUPPORTED_PADDING_MODES = 11,
    GET_SUPPORTED_DIGESTS = 12,
    GET_SUPPORTED_IMPORT_FORMATS = 13,
    GET_SUPPORTED_EXPORT_FORMATS = 14,
    GET_KEY_CHARACTERISTICS = 15,
};

/**
 * Keymaster message versions are tied to keymaster versions.  We map the keymaster
 * major.minor.subminor version to a sequential "message version".
 *
 * Rather than encoding a version number into each message we rely on the client -- who initiates
 * all requests -- to check the version of the keymaster implementation with the GET_VERSION command
 * and to send only requests that the implementation can understand.  This means that only the
 * client side needs to manage version compatibility; the implementation can always expect/produce
 * messages of its format.
 *
 * Because message version selection is purely a client-side issue, all messages default to using
 * the latest version (MAX_MESSAGE_VERSION).  Client code must take care to check versions and pass
 * correct version values to message constructors.  The AndroidKeymaster implementation always uses
 * the default, latest.
 *
 * Note that this approach implies that GetVersionRequest and GetVersionResponse cannot be
 * versioned.
 */
const int32_t MAX_MESSAGE_VERSION = 2;
inline int32_t MessageVersion(uint8_t major_ver, uint8_t minor_ver, uint8_t /* subminor_ver */) {
    int32_t message_version = -1;
    switch (major_ver) {
    case 0:
        // For the moment we still support version 0, though in general the plan is not to support
        // non-matching major versions.
        message_version = 0;
        break;
    case 1:
        switch (minor_ver) {
        case 0:
            message_version = 1;
            break;
        case 1:
            message_version = 2;
            break;
        }
    };
    return message_version;
}

struct KeymasterMessage : public Serializable {
    KeymasterMessage(int32_t ver) : message_version(ver) { assert(ver >= 0); }
    uint32_t message_version;
};

/**
 * All responses include an error value, and if the error is not KM_ERROR_OK, return no additional
 * data.  This abstract class factors out the common serialization functionality for all of the
 * responses, so we only have to implement it once.  Inheritance for reuse is generally not a great
 * structure, but in this case it's the cleanest option.
 */
struct KeymasterResponse : public KeymasterMessage {
    explicit KeymasterResponse(int32_t ver)
        : KeymasterMessage(ver), error(KM_ERROR_UNKNOWN_ERROR) {}

    size_t SerializedSize() const override;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    virtual size_t NonErrorSerializedSize() const = 0;
    virtual uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const = 0;
    virtual bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) = 0;

    keymaster_error_t error;
};

struct SupportedAlgorithmsRequest : public KeymasterMessage {
    explicit SupportedAlgorithmsRequest(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterMessage(ver) {}

    size_t SerializedSize() const override { return 0; };
    uint8_t* Serialize(uint8_t* buf, const uint8_t* /* end */) const override { return buf; }
    bool Deserialize(const uint8_t** /* buf_ptr */, const uint8_t* /* end */) override {
        return true;
    }
};

struct SupportedByAlgorithmRequest : public KeymasterMessage {
    explicit SupportedByAlgorithmRequest(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterMessage(ver) {}

    size_t SerializedSize() const override { return sizeof(uint32_t); };
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override {
        return append_uint32_to_buf(buf, end, algorithm);
    }
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override {
        return copy_uint32_from_buf(buf_ptr, end, &algorithm);
    }

    keymaster_algorithm_t algorithm;
};

class SupportedImportFormatsRequest : public SupportedByAlgorithmRequest {};
class SupportedExportFormatsRequest : public SupportedByAlgorithmRequest {};

struct SupportedByAlgorithmAndPurposeRequest : public KeymasterMessage {
    explicit SupportedByAlgorithmAndPurposeRequest(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterMessage(ver) {}

    size_t SerializedSize() const override { return sizeof(uint32_t) * 2; };
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override {
        buf = append_uint32_to_buf(buf, end, algorithm);
        return append_uint32_to_buf(buf, end, purpose);
    }
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override {
        return copy_uint32_from_buf(buf_ptr, end, &algorithm) &&
               copy_uint32_from_buf(buf_ptr, end, &purpose);
    }

    keymaster_algorithm_t algorithm;
    keymaster_purpose_t purpose;
};

class SupportedBlockModesRequest : public SupportedByAlgorithmAndPurposeRequest {};
class SupportedPaddingModesRequest : public SupportedByAlgorithmAndPurposeRequest {};
class SupportedDigestsRequest : public SupportedByAlgorithmAndPurposeRequest {};

template <typename T> struct SupportedResponse : public KeymasterResponse {
    explicit SupportedResponse(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterResponse(ver), results(NULL), results_length(0) {}
    ~SupportedResponse() { delete[] results; }

    template <size_t N> void SetResults(const T(&arr)[N]) { SetResults(arr, N); }

    void SetResults(const T* arr, size_t n) {
        delete[] results;
        results_length = 0;
        results = dup_array(arr, n);
        if (results == NULL) {
            error = KM_ERROR_MEMORY_ALLOCATION_FAILED;
        } else {
            results_length = n;
            error = KM_ERROR_OK;
        }
    }

    size_t NonErrorSerializedSize() const override {
        return sizeof(uint32_t) + results_length * sizeof(uint32_t);
    }
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override {
        return append_uint32_array_to_buf(buf, end, results, results_length);
    }
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override {
        delete[] results;
        results = NULL;
        UniquePtr<T[]> tmp;
        if (!copy_uint32_array_from_buf(buf_ptr, end, &tmp, &results_length))
            return false;
        results = tmp.release();
        return true;
    }

    T* results;
    size_t results_length;
};

class SupportedAlgorithmsResponse : public SupportedResponse<keymaster_algorithm_t> {};
class SupportedBlockModesResponse : public SupportedResponse<keymaster_block_mode_t> {};
class SupportedPaddingModesResponse : public SupportedResponse<keymaster_padding_t> {};
class SupportedDigestsResponse : public SupportedResponse<keymaster_digest_t> {};
class SupportedImportFormatsResponse : public SupportedResponse<keymaster_key_format_t> {};
class SupportedExportFormatsResponse : public SupportedResponse<keymaster_key_format_t> {};

struct GenerateKeyRequest : public KeymasterMessage {
    explicit GenerateKeyRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const override { return key_description.SerializedSize(); }
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override {
        return key_description.Serialize(buf, end);
    }
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override {
        return key_description.Deserialize(buf_ptr, end);
    }

    AuthorizationSet key_description;
};

struct GenerateKeyResponse : public KeymasterResponse {
    explicit GenerateKeyResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {
        key_blob.key_material = NULL;
        key_blob.key_material_size = 0;
    }
    ~GenerateKeyResponse();

    size_t NonErrorSerializedSize() const override;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    keymaster_key_blob_t key_blob;
    AuthorizationSet enforced;
    AuthorizationSet unenforced;
};

struct GetKeyCharacteristicsRequest : public KeymasterMessage {
    explicit GetKeyCharacteristicsRequest(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterMessage(ver) {
        key_blob.key_material = NULL;
        key_blob.key_material_size = 0;
    }
    ~GetKeyCharacteristicsRequest();

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t SerializedSize() const override;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    keymaster_key_blob_t key_blob;
    AuthorizationSet additional_params;
};

struct GetKeyCharacteristicsResponse : public KeymasterResponse {
    explicit GetKeyCharacteristicsResponse(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterResponse(ver) {}
    size_t NonErrorSerializedSize() const override;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    AuthorizationSet enforced;
    AuthorizationSet unenforced;
};

struct BeginOperationRequest : public KeymasterMessage {
    explicit BeginOperationRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {
        key_blob.key_material = NULL;
        key_blob.key_material_size = 0;
    }
    ~BeginOperationRequest() { delete[] key_blob.key_material; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    keymaster_purpose_t purpose;
    keymaster_key_blob_t key_blob;
    AuthorizationSet additional_params;
};

struct BeginOperationResponse : public KeymasterResponse {
    explicit BeginOperationResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}

    size_t NonErrorSerializedSize() const override;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    keymaster_operation_handle_t op_handle;
    AuthorizationSet output_params;
};

struct UpdateOperationRequest : public KeymasterMessage {
    explicit UpdateOperationRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const override;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    keymaster_operation_handle_t op_handle;
    Buffer input;
    AuthorizationSet additional_params;
};

struct UpdateOperationResponse : public KeymasterResponse {
    explicit UpdateOperationResponse(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterResponse(ver), input_consumed(0) {}

    size_t NonErrorSerializedSize() const override;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    Buffer output;
    size_t input_consumed;
    AuthorizationSet output_params;
};

struct FinishOperationRequest : public KeymasterMessage {
    explicit FinishOperationRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const override;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    keymaster_operation_handle_t op_handle;
    Buffer signature;
    AuthorizationSet additional_params;
};

struct FinishOperationResponse : public KeymasterResponse {
    explicit FinishOperationResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}

    size_t NonErrorSerializedSize() const override;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    Buffer output;
    AuthorizationSet output_params;
};

struct AbortOperationRequest : public KeymasterMessage {
    explicit AbortOperationRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const override { return sizeof(uint64_t); }
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override {
        return append_uint64_to_buf(buf, end, op_handle);
    }
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override {
        return copy_uint64_from_buf(buf_ptr, end, &op_handle);
    }

    keymaster_operation_handle_t op_handle;
};

struct AbortOperationResponse : public KeymasterResponse {
    explicit AbortOperationResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}

    size_t NonErrorSerializedSize() const override { return 0; }
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t*) const override { return buf; }
    bool NonErrorDeserialize(const uint8_t**, const uint8_t*) override { return true; }
};

struct AddEntropyRequest : public KeymasterMessage {
    explicit AddEntropyRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const override;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    Buffer random_data;
};

struct AddEntropyResponse : public KeymasterResponse {
    explicit AddEntropyResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}

    size_t NonErrorSerializedSize() const override { return 0; }
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* /* end */) const override {
        return buf;
    }
    bool NonErrorDeserialize(const uint8_t** /* buf_ptr */, const uint8_t* /* end */) override {
        return true;
    }
};

struct ImportKeyRequest : public KeymasterMessage {
    explicit ImportKeyRequest(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterMessage(ver), key_data(NULL) {}
    ~ImportKeyRequest() { delete[] key_data; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t SerializedSize() const override;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    AuthorizationSet key_description;
    keymaster_key_format_t key_format;
    uint8_t* key_data;
    size_t key_data_length;
};

struct ImportKeyResponse : public KeymasterResponse {
    explicit ImportKeyResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {
        key_blob.key_material = NULL;
        key_blob.key_material_size = 0;
    }
    ~ImportKeyResponse() { delete[] key_blob.key_material; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t NonErrorSerializedSize() const override;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    keymaster_key_blob_t key_blob;
    AuthorizationSet enforced;
    AuthorizationSet unenforced;
};

struct ExportKeyRequest : public KeymasterMessage {
    explicit ExportKeyRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {
        key_blob.key_material = NULL;
        key_blob.key_material_size = 0;
    }
    ~ExportKeyRequest() { delete[] key_blob.key_material; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t SerializedSize() const override;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    AuthorizationSet additional_params;
    keymaster_key_format_t key_format;
    keymaster_key_blob_t key_blob;
};

struct ExportKeyResponse : public KeymasterResponse {
    explicit ExportKeyResponse(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterResponse(ver), key_data(NULL) {}
    ~ExportKeyResponse() { delete[] key_data; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t NonErrorSerializedSize() const override;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    uint8_t* key_data;
    size_t key_data_length;
};

struct DeleteKeyRequest : public KeymasterMessage {
    explicit DeleteKeyRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {
        key_blob.key_material = nullptr;
        key_blob.key_material_size = 0;
    }
    ~DeleteKeyRequest() { delete[] key_blob.key_material; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t SerializedSize() const override;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    keymaster_key_blob_t key_blob;
};

struct DeleteKeyResponse : public KeymasterResponse {
    explicit DeleteKeyResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}

    size_t NonErrorSerializedSize() const override { return 0; }
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t*) const override { return buf; }
    bool NonErrorDeserialize(const uint8_t**, const uint8_t*) override { return true; }
};

struct DeleteAllKeysRequest : public KeymasterMessage {
    explicit DeleteAllKeysRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const override { return 0; }
    uint8_t* Serialize(uint8_t* buf, const uint8_t*) const override { return buf; }
    bool Deserialize(const uint8_t**, const uint8_t*) override { return true; };
};

struct DeleteAllKeysResponse : public KeymasterResponse {
    explicit DeleteAllKeysResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}

    size_t NonErrorSerializedSize() const override { return 0; }
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t*) const override { return buf; }
    bool NonErrorDeserialize(const uint8_t**, const uint8_t*) override { return true; }
};

struct GetVersionRequest : public KeymasterMessage {
    explicit GetVersionRequest() : KeymasterMessage(0 /* not versionable */) {}

    size_t SerializedSize() const override { return 0; }
    uint8_t* Serialize(uint8_t* buf, const uint8_t*) const override { return buf; }
    bool Deserialize(const uint8_t**, const uint8_t*) override { return true; };
};

struct GetVersionResponse : public KeymasterResponse {
    explicit GetVersionResponse()
        : KeymasterResponse(0 /* not versionable */), major_ver(0), minor_ver(0), subminor_ver(0) {}

    size_t NonErrorSerializedSize() const override;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const override;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) override;

    uint8_t major_ver;
    uint8_t minor_ver;
    uint8_t subminor_ver;
};

}  // namespace keymaster

#endif  // SYSTEM_KEYMASTER_ANDROID_KEYMASTER_MESSAGES_H_
