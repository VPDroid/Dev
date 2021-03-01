/*
 * Copyright (C) 2014 The Android Open Source Project
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

// Uncomment for verbose logging.
// #define LOG_NDEBUG 0
#define LOG_TAG "webviewchromiumloader"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <jni.h>
#include <android/dlext.h>
#include <utils/Log.h>

#define NELEM(x) ((int) (sizeof(x) / sizeof((x)[0])))

namespace android {
namespace {

void* gReservedAddress = NULL;
size_t gReservedSize = 0;

jint LIBLOAD_SUCCESS;
jint LIBLOAD_FAILED_TO_OPEN_RELRO_FILE;
jint LIBLOAD_FAILED_TO_LOAD_LIBRARY;
jint LIBLOAD_FAILED_JNI_CALL;

jboolean DoReserveAddressSpace(jlong size) {
  size_t vsize = static_cast<size_t>(size);

  void* addr = mmap(NULL, vsize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED) {
    ALOGE("Failed to reserve %zd bytes of address space for future load of "
          "libwebviewchromium.so: %s",
          vsize, strerror(errno));
    return JNI_FALSE;
  }
  gReservedAddress = addr;
  gReservedSize = vsize;
  ALOGV("Reserved %zd bytes at %p", vsize, addr);
  return JNI_TRUE;
}

jboolean DoCreateRelroFile(const char* lib, const char* relro) {
  // Try to unlink the old file, since if this is being called, the old one is
  // obsolete.
  if (unlink(relro) != 0 && errno != ENOENT) {
    // If something went wrong other than the file not existing, log a warning
    // but continue anyway in the hope that we can successfully overwrite the
    // existing file with rename() later.
    ALOGW("Failed to unlink old file %s: %s", relro, strerror(errno));
  }
  static const char tmpsuffix[] = ".XXXXXX";
  char relro_tmp[strlen(relro) + sizeof(tmpsuffix)];
  strlcpy(relro_tmp, relro, sizeof(relro_tmp));
  strlcat(relro_tmp, tmpsuffix, sizeof(relro_tmp));
  int tmp_fd = TEMP_FAILURE_RETRY(mkstemp(relro_tmp));
  if (tmp_fd == -1) {
    ALOGE("Failed to create temporary file %s: %s", relro_tmp, strerror(errno));
    return JNI_FALSE;
  }
  android_dlextinfo extinfo;
  extinfo.flags = ANDROID_DLEXT_RESERVED_ADDRESS | ANDROID_DLEXT_WRITE_RELRO;
  extinfo.reserved_addr = gReservedAddress;
  extinfo.reserved_size = gReservedSize;
  extinfo.relro_fd = tmp_fd;
  void* handle = android_dlopen_ext(lib, RTLD_NOW, &extinfo);
  int close_result = close(tmp_fd);
  if (handle == NULL) {
    ALOGE("Failed to load library %s: %s", lib, dlerror());
    unlink(relro_tmp);
    return JNI_FALSE;
  }
  if (close_result != 0 ||
      chmod(relro_tmp, S_IRUSR | S_IRGRP | S_IROTH) != 0 ||
      rename(relro_tmp, relro) != 0) {
    ALOGE("Failed to update relro file %s: %s", relro, strerror(errno));
    unlink(relro_tmp);
    return JNI_FALSE;
  }
  ALOGV("Created relro file %s for library %s", relro, lib);
  return JNI_TRUE;
}

jint DoLoadWithRelroFile(const char* lib, const char* relro) {
  int relro_fd = TEMP_FAILURE_RETRY(open(relro, O_RDONLY));
  if (relro_fd == -1) {
    ALOGE("Failed to open relro file %s: %s", relro, strerror(errno));
    return LIBLOAD_FAILED_TO_OPEN_RELRO_FILE;
  }
  android_dlextinfo extinfo;
  extinfo.flags = ANDROID_DLEXT_RESERVED_ADDRESS | ANDROID_DLEXT_USE_RELRO;
  extinfo.reserved_addr = gReservedAddress;
  extinfo.reserved_size = gReservedSize;
  extinfo.relro_fd = relro_fd;
  void* handle = android_dlopen_ext(lib, RTLD_NOW, &extinfo);
  close(relro_fd);
  if (handle == NULL) {
    ALOGE("Failed to load library %s: %s", lib, dlerror());
    return LIBLOAD_FAILED_TO_LOAD_LIBRARY;
  }
  ALOGV("Loaded library %s with relro file %s", lib, relro);
  return LIBLOAD_SUCCESS;
}

/******************************************************************************/
/* JNI wrappers - handle string lifetimes and 32/64 ABI choice                */
/******************************************************************************/

jboolean ReserveAddressSpace(JNIEnv*, jclass, jlong size) {
  return DoReserveAddressSpace(size);
}

jboolean CreateRelroFile(JNIEnv* env, jclass, jstring lib32, jstring lib64,
                         jstring relro32, jstring relro64) {
#ifdef __LP64__
  jstring lib = lib64;
  jstring relro = relro64;
  (void)lib32; (void)relro32;
#else
  jstring lib = lib32;
  jstring relro = relro32;
  (void)lib64; (void)relro64;
#endif
  jboolean ret = JNI_FALSE;
  const char* lib_utf8 = env->GetStringUTFChars(lib, NULL);
  if (lib_utf8 != NULL) {
    const char* relro_utf8 = env->GetStringUTFChars(relro, NULL);
    if (relro_utf8 != NULL) {
      ret = DoCreateRelroFile(lib_utf8, relro_utf8);
      env->ReleaseStringUTFChars(relro, relro_utf8);
    }
    env->ReleaseStringUTFChars(lib, lib_utf8);
  }
  return ret;
}

jint LoadWithRelroFile(JNIEnv* env, jclass, jstring lib32, jstring lib64,
                           jstring relro32, jstring relro64) {
#ifdef __LP64__
  jstring lib = lib64;
  jstring relro = relro64;
  (void)lib32; (void)relro32;
#else
  jstring lib = lib32;
  jstring relro = relro32;
  (void)lib64; (void)relro64;
#endif
  jint ret = LIBLOAD_FAILED_JNI_CALL;
  const char* lib_utf8 = env->GetStringUTFChars(lib, NULL);
  if (lib_utf8 != NULL) {
    const char* relro_utf8 = env->GetStringUTFChars(relro, NULL);
    if (relro_utf8 != NULL) {
      ret = DoLoadWithRelroFile(lib_utf8, relro_utf8);
      env->ReleaseStringUTFChars(relro, relro_utf8);
    }
    env->ReleaseStringUTFChars(lib, lib_utf8);
  }
  return ret;
}

const char kClassName[] = "android/webkit/WebViewFactory";
const JNINativeMethod kJniMethods[] = {
  { "nativeReserveAddressSpace", "(J)Z",
      reinterpret_cast<void*>(ReserveAddressSpace) },
  { "nativeCreateRelroFile",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
      reinterpret_cast<void*>(CreateRelroFile) },
  { "nativeLoadWithRelroFile",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
      reinterpret_cast<void*>(LoadWithRelroFile) },
};

}  // namespace

void RegisterWebViewFactory(JNIEnv* env) {
  // If either of these fail, it will set an exception that will be thrown on
  // return, so no need to handle errors here.
  jclass clazz = env->FindClass(kClassName);
  if (clazz) {
    env->RegisterNatives(clazz, kJniMethods, NELEM(kJniMethods));

    LIBLOAD_SUCCESS = env->GetStaticIntField(
        clazz,
        env->GetStaticFieldID(clazz, "LIBLOAD_SUCCESS", "I"));

    LIBLOAD_FAILED_TO_OPEN_RELRO_FILE = env->GetStaticIntField(
        clazz,
        env->GetStaticFieldID(clazz, "LIBLOAD_FAILED_TO_OPEN_RELRO_FILE", "I"));

    LIBLOAD_FAILED_TO_LOAD_LIBRARY = env->GetStaticIntField(
        clazz,
        env->GetStaticFieldID(clazz, "LIBLOAD_FAILED_TO_LOAD_LIBRARY", "I"));

    LIBLOAD_FAILED_JNI_CALL = env->GetStaticIntField(
        clazz,
        env->GetStaticFieldID(clazz, "LIBLOAD_FAILED_JNI_CALL", "I"));
  }
}

}  // namespace android

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = NULL;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    ALOGE("GetEnv failed");
    return JNI_ERR;
  }
  android::RegisterWebViewFactory(env);
  return JNI_VERSION_1_6;
}
