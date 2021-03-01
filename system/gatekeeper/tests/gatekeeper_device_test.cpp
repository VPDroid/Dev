/*
 * Copyright (C) 2015 The Android Open Source Project
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
#include <gtest/gtest.h>
#include <hardware/gatekeeper.h>
#include <gatekeeper/gatekeeper.h> // For password_handle_t

using ::testing::Test;
using ::gatekeeper::password_handle_t;
using ::gatekeeper::secure_id_t;

class GateKeeperDeviceTest : public virtual Test {
public:
    GateKeeperDeviceTest() {}
    virtual ~GateKeeperDeviceTest() {}

    virtual void SetUp() {
        gatekeeper_device_initialize(&device);
    }

    virtual void TearDown() {
        gatekeeper_close(device);
    }

    static void gatekeeper_device_initialize(gatekeeper_device_t **dev) {
        int ret;
        const hw_module_t *mod;
        ret = hw_get_module_by_class(GATEKEEPER_HARDWARE_MODULE_ID, NULL, &mod);

        ASSERT_EQ(0, ret);

        ret = gatekeeper_open(mod, dev);

        ASSERT_EQ(0, ret);
    }

    gatekeeper_device_t *device;
};

TEST_F(GateKeeperDeviceTest, EnrollAndVerify) {
    uint32_t password_len = 50;
    uint8_t password_payload[password_len];
    uint8_t *password_handle;
    uint32_t password_handle_length;
    uint8_t *auth_token;
    uint32_t auth_token_len;
    int ret;

    ret = device->enroll(device, 0, NULL, 0, NULL, 0,  password_payload, password_len,
            &password_handle, &password_handle_length);

    ASSERT_EQ(0, ret);

    bool should_reenroll;
    ret = device->verify(device, 0, 0, password_handle, password_handle_length,
            password_payload, password_len, &auth_token, &auth_token_len, &should_reenroll);

    ASSERT_EQ(0, ret);
}

TEST_F(GateKeeperDeviceTest, EnrollAndVerifyBadPassword) {
    uint32_t password_len = 50;
    uint8_t password_payload[password_len];
    uint8_t *password_handle;
    uint32_t password_handle_length;
    uint8_t *auth_token = NULL;
    uint32_t auth_token_len;
    int ret;

    ret = device->enroll(device, 0, NULL, 0, NULL, 0,  password_payload, password_len,
             &password_handle, &password_handle_length);

    ASSERT_EQ(0, ret);

    password_payload[0] = 4;

    bool should_reenroll;
    ret = device->verify(device, 0, 0, password_handle, password_handle_length,
            password_payload, password_len, &auth_token, &auth_token_len,
            &should_reenroll);

    ASSERT_NE(0, ret);
    ASSERT_EQ(NULL, auth_token);
}

TEST_F(GateKeeperDeviceTest, UntrustedReEnroll) {
    uint32_t password_len = 50;
    uint8_t password_payload[password_len];
    uint8_t *password_handle;
    uint32_t password_handle_length;
    int ret;

    ret = device->enroll(device, 0, NULL, 0, NULL, 0, password_payload, password_len,
             &password_handle, &password_handle_length);

    ASSERT_EQ(0, ret);

    password_handle_t *handle = reinterpret_cast<password_handle_t *>(password_handle);
    secure_id_t sid = handle->user_id;

    ret = device->enroll(device, 0, NULL, 0, NULL, 0, password_payload, password_len,
            &password_handle, &password_handle_length);

    ASSERT_EQ(0, ret);
    handle = reinterpret_cast<password_handle_t *>(password_handle);
    ASSERT_NE(sid, handle->user_id);
}


TEST_F(GateKeeperDeviceTest, TrustedReEnroll) {
    uint32_t password_len = 50;
    uint8_t password_payload[password_len];
    uint8_t *password_handle;
    uint32_t password_handle_length;
    int ret;

    ret = device->enroll(device, 0, NULL, 0, NULL, 0, password_payload, password_len,
             &password_handle, &password_handle_length);

    ASSERT_EQ(0, ret);

    password_handle_t *handle = reinterpret_cast<password_handle_t *>(password_handle);
    secure_id_t sid = handle->user_id;

    ret = device->enroll(device, 0, password_handle, password_handle_length, password_payload,
            password_len, password_payload, password_len, &password_handle, &password_handle_length);

    ASSERT_EQ(0, ret);
    handle = reinterpret_cast<password_handle_t *>(password_handle);
    ASSERT_EQ(sid, handle->user_id);
}

