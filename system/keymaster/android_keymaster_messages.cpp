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

#include <keymaster/android_keymaster_messages.h>
#include <keymaster/android_keymaster_utils.h>

namespace keymaster {

/*
 * Helper functions for working with key blobs.
 */

static void set_key_blob(keymaster_key_blob_t* key_blob, const void* key_material, size_t length) {
    delete[] key_blob->key_material;
    key_blob->key_material = dup_buffer(key_material, length);
    key_blob->key_material_size = length;
}

static size_t key_blob_size(const keymaster_key_blob_t& key_blob) {
    return sizeof(uint32_t) /* key size */ + key_blob.key_material_size;
}

static uint8_t* serialize_key_blob(const keymaster_key_blob_t& key_blob, uint8_t* buf,
                                   const uint8_t* end) {
    return append_size_and_data_to_buf(buf, end, key_blob.key_material, key_blob.key_material_size);
}

static bool deserialize_key_blob(keymaster_key_blob_t* key_blob, const uint8_t** buf_ptr,
                                 const uint8_t* end) {
    delete[] key_blob->key_material;
    key_blob->key_material = 0;
    UniquePtr<uint8_t[]> deserialized_key_material;
    if (!copy_size_and_data_from_buf(buf_ptr, end, &key_blob->key_material_size,
                                     &deserialized_key_material))
        return false;
    key_blob->key_material = deserialized_key_material.release();
    return true;
}

size_t KeymasterResponse::SerializedSize() const {
    if (error != KM_ERROR_OK)
        return sizeof(int32_t);
    else
        return sizeof(int32_t) + NonErrorSerializedSize();
}

uint8_t* KeymasterResponse::Serialize(uint8_t* buf, const uint8_t* end) const {
    buf = append_uint32_to_buf(buf, end, static_cast<uint32_t>(error));
    if (error == KM_ERROR_OK)
        buf = NonErrorSerialize(buf, end);
    return buf;
}

bool KeymasterResponse::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    if (!copy_uint32_from_buf(buf_ptr, end, &error))
        return false;
    if (error != KM_ERROR_OK)
        return true;
    return NonErrorDeserialize(buf_ptr, end);
}

GenerateKeyResponse::~GenerateKeyResponse() {
    delete[] key_blob.key_material;
}

size_t GenerateKeyResponse::NonErrorSerializedSize() const {
    return key_blob_size(key_blob) + enforced.SerializedSize() + unenforced.SerializedSize();
}

uint8_t* GenerateKeyResponse::NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
    buf = serialize_key_blob(key_blob, buf, end);
    buf = enforced.Serialize(buf, end);
    return unenforced.Serialize(buf, end);
}

bool GenerateKeyResponse::NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    return deserialize_key_blob(&key_blob, buf_ptr, end) && enforced.Deserialize(buf_ptr, end) &&
           unenforced.Deserialize(buf_ptr, end);
}

GetKeyCharacteristicsRequest::~GetKeyCharacteristicsRequest() {
    delete[] key_blob.key_material;
}

void GetKeyCharacteristicsRequest::SetKeyMaterial(const void* key_material, size_t length) {
    set_key_blob(&key_blob, key_material, length);
}

size_t GetKeyCharacteristicsRequest::SerializedSize() const {
    return key_blob_size(key_blob) + additional_params.SerializedSize();
}

uint8_t* GetKeyCharacteristicsRequest::Serialize(uint8_t* buf, const uint8_t* end) const {
    buf = serialize_key_blob(key_blob, buf, end);
    return additional_params.Serialize(buf, end);
}

bool GetKeyCharacteristicsRequest::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    return deserialize_key_blob(&key_blob, buf_ptr, end) &&
           additional_params.Deserialize(buf_ptr, end);
}

size_t GetKeyCharacteristicsResponse::NonErrorSerializedSize() const {
    return enforced.SerializedSize() + unenforced.SerializedSize();
}

uint8_t* GetKeyCharacteristicsResponse::NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
    buf = enforced.Serialize(buf, end);
    return unenforced.Serialize(buf, end);
}

bool GetKeyCharacteristicsResponse::NonErrorDeserialize(const uint8_t** buf_ptr,
                                                        const uint8_t* end) {
    return enforced.Deserialize(buf_ptr, end) && unenforced.Deserialize(buf_ptr, end);
}

void BeginOperationRequest::SetKeyMaterial(const void* key_material, size_t length) {
    set_key_blob(&key_blob, key_material, length);
}

size_t BeginOperationRequest::SerializedSize() const {
    return sizeof(uint32_t) /* purpose */ + key_blob_size(key_blob) +
           additional_params.SerializedSize();
}

uint8_t* BeginOperationRequest::Serialize(uint8_t* buf, const uint8_t* end) const {
    buf = append_uint32_to_buf(buf, end, purpose);
    buf = serialize_key_blob(key_blob, buf, end);
    return additional_params.Serialize(buf, end);
}

bool BeginOperationRequest::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    return copy_uint32_from_buf(buf_ptr, end, &purpose) &&
           deserialize_key_blob(&key_blob, buf_ptr, end) &&
           additional_params.Deserialize(buf_ptr, end);
}

size_t BeginOperationResponse::NonErrorSerializedSize() const {
    if (message_version == 0)
        return sizeof(op_handle);
    else
        return sizeof(op_handle) + output_params.SerializedSize();
}

uint8_t* BeginOperationResponse::NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
    buf = append_uint64_to_buf(buf, end, op_handle);
    if (message_version > 0)
        buf = output_params.Serialize(buf, end);
    return buf;
}

bool BeginOperationResponse::NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    bool retval = copy_uint64_from_buf(buf_ptr, end, &op_handle);
    if (retval && message_version > 0)
        retval = output_params.Deserialize(buf_ptr, end);
    return retval;
}

size_t UpdateOperationRequest::SerializedSize() const {
    if (message_version == 0)
        return sizeof(op_handle) + input.SerializedSize();
    else
        return sizeof(op_handle) + input.SerializedSize() + additional_params.SerializedSize();
}

uint8_t* UpdateOperationRequest::Serialize(uint8_t* buf, const uint8_t* end) const {
    buf = append_uint64_to_buf(buf, end, op_handle);
    buf = input.Serialize(buf, end);
    if (message_version > 0)
        buf = additional_params.Serialize(buf, end);
    return buf;
}

bool UpdateOperationRequest::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    bool retval = copy_uint64_from_buf(buf_ptr, end, &op_handle) && input.Deserialize(buf_ptr, end);
    if (retval && message_version > 0)
        retval = additional_params.Deserialize(buf_ptr, end);
    return retval;
}

size_t UpdateOperationResponse::NonErrorSerializedSize() const {
    switch (message_version) {
    case 0:
        return output.SerializedSize();
    case 1:
        return output.SerializedSize() + sizeof(uint32_t);
    case 2:
        return output.SerializedSize() + sizeof(uint32_t) + output_params.SerializedSize();
    default:
        assert(false);
        return 0;
    }
}

uint8_t* UpdateOperationResponse::NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
    buf = output.Serialize(buf, end);
    if (message_version > 0)
        buf = append_uint32_to_buf(buf, end, input_consumed);
    if (message_version > 1)
        buf = output_params.Serialize(buf, end);
    return buf;
}

bool UpdateOperationResponse::NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    bool retval = output.Deserialize(buf_ptr, end);
    if (retval && message_version > 0)
        retval = copy_uint32_from_buf(buf_ptr, end, &input_consumed);
    if (retval && message_version > 1)
        retval = output_params.Deserialize(buf_ptr, end);
    return retval;
}

size_t FinishOperationRequest::SerializedSize() const {
    if (message_version == 0)
        return sizeof(op_handle) + signature.SerializedSize();
    else
        return sizeof(op_handle) + signature.SerializedSize() + additional_params.SerializedSize();
}

uint8_t* FinishOperationRequest::Serialize(uint8_t* buf, const uint8_t* end) const {
    buf = append_uint64_to_buf(buf, end, op_handle);
    buf = signature.Serialize(buf, end);
    if (message_version > 0)
        buf = additional_params.Serialize(buf, end);
    return buf;
}

bool FinishOperationRequest::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    bool retval =
        copy_uint64_from_buf(buf_ptr, end, &op_handle) && signature.Deserialize(buf_ptr, end);
    if (retval && message_version > 0)
        retval = additional_params.Deserialize(buf_ptr, end);
    return retval;
}

size_t FinishOperationResponse::NonErrorSerializedSize() const {
    if (message_version < 2)
        return output.SerializedSize();
    else
        return output.SerializedSize() + output_params.SerializedSize();
}

uint8_t* FinishOperationResponse::NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
    buf = output.Serialize(buf, end);
    if (message_version > 1)
        buf = output_params.Serialize(buf, end);
    return buf;
}

bool FinishOperationResponse::NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    bool retval = output.Deserialize(buf_ptr, end);
    if (retval && message_version > 1)
        retval = output_params.Deserialize(buf_ptr, end);
    return retval;
}

size_t AddEntropyRequest::SerializedSize() const {
    return random_data.SerializedSize();
}

uint8_t* AddEntropyRequest::Serialize(uint8_t* buf, const uint8_t* end) const {
    return random_data.Serialize(buf, end);
}

bool AddEntropyRequest::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    return random_data.Deserialize(buf_ptr, end);
}

void ImportKeyRequest::SetKeyMaterial(const void* key_material, size_t length) {
    delete[] key_data;
    key_data = dup_buffer(key_material, length);
    key_data_length = length;
}

size_t ImportKeyRequest::SerializedSize() const {
    return key_description.SerializedSize() + sizeof(uint32_t) /* key_format */ +
           sizeof(uint32_t) /* key_data_length */ + key_data_length;
}

uint8_t* ImportKeyRequest::Serialize(uint8_t* buf, const uint8_t* end) const {
    buf = key_description.Serialize(buf, end);
    buf = append_uint32_to_buf(buf, end, key_format);
    return append_size_and_data_to_buf(buf, end, key_data, key_data_length);
}

bool ImportKeyRequest::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    delete[] key_data;
    key_data = NULL;
    UniquePtr<uint8_t[]> deserialized_key_material;
    if (!key_description.Deserialize(buf_ptr, end) ||
        !copy_uint32_from_buf(buf_ptr, end, &key_format) ||
        !copy_size_and_data_from_buf(buf_ptr, end, &key_data_length, &deserialized_key_material))
        return false;
    key_data = deserialized_key_material.release();
    return true;
}

void ImportKeyResponse::SetKeyMaterial(const void* key_material, size_t length) {
    set_key_blob(&key_blob, key_material, length);
}

size_t ImportKeyResponse::NonErrorSerializedSize() const {
    return key_blob_size(key_blob) + enforced.SerializedSize() + unenforced.SerializedSize();
}

uint8_t* ImportKeyResponse::NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
    buf = serialize_key_blob(key_blob, buf, end);
    buf = enforced.Serialize(buf, end);
    return unenforced.Serialize(buf, end);
}

bool ImportKeyResponse::NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    return deserialize_key_blob(&key_blob, buf_ptr, end) && enforced.Deserialize(buf_ptr, end) &&
           unenforced.Deserialize(buf_ptr, end);
}

void ExportKeyRequest::SetKeyMaterial(const void* key_material, size_t length) {
    set_key_blob(&key_blob, key_material, length);
}

size_t ExportKeyRequest::SerializedSize() const {
    return additional_params.SerializedSize() + sizeof(uint32_t) /* key_format */ +
           key_blob_size(key_blob);
}

uint8_t* ExportKeyRequest::Serialize(uint8_t* buf, const uint8_t* end) const {
    buf = additional_params.Serialize(buf, end);
    buf = append_uint32_to_buf(buf, end, key_format);
    return serialize_key_blob(key_blob, buf, end);
}

bool ExportKeyRequest::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    return additional_params.Deserialize(buf_ptr, end) &&
           copy_uint32_from_buf(buf_ptr, end, &key_format) &&
           deserialize_key_blob(&key_blob, buf_ptr, end);
}

void ExportKeyResponse::SetKeyMaterial(const void* key_material, size_t length) {
    delete[] key_data;
    key_data = dup_buffer(key_material, length);
    key_data_length = length;
}

size_t ExportKeyResponse::NonErrorSerializedSize() const {
    return sizeof(uint32_t) /* key_data_length */ + key_data_length;
}

uint8_t* ExportKeyResponse::NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
    return append_size_and_data_to_buf(buf, end, key_data, key_data_length);
}

bool ExportKeyResponse::NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    delete[] key_data;
    key_data = NULL;
    UniquePtr<uint8_t[]> deserialized_key_material;
    if (!copy_size_and_data_from_buf(buf_ptr, end, &key_data_length, &deserialized_key_material))
        return false;
    key_data = deserialized_key_material.release();
    return true;
}

void DeleteKeyRequest::SetKeyMaterial(const void* key_material, size_t length) {
    set_key_blob(&key_blob, key_material, length);
}

size_t DeleteKeyRequest::SerializedSize() const {
    return key_blob_size(key_blob);
}

uint8_t* DeleteKeyRequest::Serialize(uint8_t* buf, const uint8_t* end) const {
    return serialize_key_blob(key_blob, buf, end);
}

bool DeleteKeyRequest::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    return deserialize_key_blob(&key_blob, buf_ptr, end);
}

size_t GetVersionResponse::NonErrorSerializedSize() const {
    return sizeof(major_ver) + sizeof(minor_ver) + sizeof(subminor_ver);
}

uint8_t* GetVersionResponse::NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
    if (buf + NonErrorSerializedSize() <= end) {
        *buf++ = major_ver;
        *buf++ = minor_ver;
        *buf++ = subminor_ver;
    } else {
        buf += NonErrorSerializedSize();
    }
    return buf;
}

bool GetVersionResponse::NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    if (*buf_ptr + NonErrorSerializedSize() > end)
        return false;
    const uint8_t* tmp = *buf_ptr;
    major_ver = *tmp++;
    minor_ver = *tmp++;
    subminor_ver = *tmp++;
    *buf_ptr = tmp;
    return true;
}

}  // namespace keymaster
