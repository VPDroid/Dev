/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#define LOG_TAG "installd"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string>
#include <vector>

#include <cutils/fs.h>
#include <cutils/sockets.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/multiuser.h>

#include <private/android_filesystem_config.h>

#if defined(__APPLE__)
#include <sys/mount.h>
#else
#include <sys/statfs.h>
#endif

#define SOCKET_PATH "installd"


/* elements combined with a valid package name to form paths */

#define PRIMARY_USER_PREFIX    "data/"
#define SECONDARY_USER_PREFIX  "user/"

#define PKG_DIR_POSTFIX        ""

#define PKG_LIB_POSTFIX        "/lib"

#define CACHE_DIR_POSTFIX      "/cache"
#define CODE_CACHE_DIR_POSTFIX "/code_cache"

#define APP_SUBDIR             "app/" // sub-directory under ANDROID_DATA
#define PRIV_APP_SUBDIR        "priv-app/" // sub-directory under ANDROID_DATA

#define APP_LIB_SUBDIR         "app-lib/" // sub-directory under ANDROID_DATA

#define MEDIA_SUBDIR           "media/" // sub-directory under ANDROID_DATA

/* other handy constants */

#define PRIVATE_APP_SUBDIR     "app-private/" // sub-directory under ANDROID_DATA

#define DALVIK_CACHE_PREFIX    "/data/dalvik-cache/"
#define DALVIK_CACHE_POSTFIX   "/classes.dex"

#define UPDATE_COMMANDS_DIR_PREFIX  "/system/etc/updatecmds/"

#define IDMAP_PREFIX           "/data/resource-cache/"
#define IDMAP_SUFFIX           "@idmap"

#define PKG_NAME_MAX  128   /* largest allowed package name */
#define PKG_PATH_MAX  256   /* max size of any path we use */

/* dexopt needed flags matching those in dalvik.system.DexFile */
#define DEXOPT_DEX2OAT_NEEDED        1
#define DEXOPT_PATCHOAT_NEEDED       2
#define DEXOPT_SELF_PATCHOAT_NEEDED  3

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

/* data structures */

typedef struct {
    char* path;
    size_t len;
} dir_rec_t;

typedef struct {
    size_t count;
    dir_rec_t* dirs;
} dir_rec_array_t;

extern dir_rec_t android_app_dir;
extern dir_rec_t android_app_private_dir;
extern dir_rec_t android_app_lib_dir;
extern dir_rec_t android_data_dir;
extern dir_rec_t android_asec_dir;
extern dir_rec_t android_media_dir;
extern dir_rec_t android_mnt_expand_dir;
extern dir_rec_array_t android_system_dirs;

typedef struct cache_dir_struct {
    struct cache_dir_struct* parent;
    int32_t childCount;
    int32_t hiddenCount;
    int32_t deleted;
    char name[];
} cache_dir_t;

typedef struct {
    cache_dir_t* dir;
    time_t modTime;
    char name[];
} cache_file_t;

typedef struct {
    size_t numDirs;
    size_t availDirs;
    cache_dir_t** dirs;
    size_t numFiles;
    size_t availFiles;
    cache_file_t** files;
    size_t numCollected;
    void* memBlocks;
    int8_t* curMemBlockAvail;
    int8_t* curMemBlockEnd;
} cache_t;

/* util.c */

int create_pkg_path(char path[PKG_PATH_MAX],
                    const char *pkgname,
                    const char *postfix,
                    userid_t userid);

std::string create_data_path(const char* volume_uuid);

std::string create_data_app_path(const char* volume_uuid);

std::string create_data_app_package_path(const char* volume_uuid, const char* package_name);

std::string create_data_user_path(const char* volume_uuid, userid_t userid);

std::string create_data_user_package_path(const char* volume_uuid,
        userid_t user, const char* package_name);

std::string create_data_media_path(const char* volume_uuid, userid_t userid);

std::vector<userid_t> get_known_users(const char* volume_uuid);

int create_user_config_path(char path[PKG_PATH_MAX], userid_t userid);

int create_move_path(char path[PKG_PATH_MAX],
                     const char* pkgname,
                     const char* leaf,
                     userid_t userid);

int is_valid_package_name(const char* pkgname);

int create_cache_path(char path[PKG_PATH_MAX], const char *src,
                      const char *instruction_set);

int delete_dir_contents(const char *pathname,
                        int also_delete_dir,
                        int (*exclusion_predicate)(const char *name, const int is_dir));

int delete_dir_contents_fd(int dfd, const char *name);

int copy_dir_files(const char *srcname, const char *dstname, uid_t owner, gid_t group);

int lookup_media_dir(char basepath[PATH_MAX], const char *dir);

int64_t data_disk_free(const std::string& data_path);

cache_t* start_cache_collection();

void add_cache_files(cache_t* cache, const char *basepath, const char *cachedir);

void clear_cache_files(const std::string& data_path, cache_t* cache, int64_t free_size);

void finish_cache_collection(cache_t* cache);

int validate_system_app_path(const char* path);

int get_path_from_env(dir_rec_t* rec, const char* var);

int get_path_from_string(dir_rec_t* rec, const char* path);

int copy_and_append(dir_rec_t* dst, const dir_rec_t* src, const char* suffix);

int validate_apk_path(const char *path);
int validate_apk_path_subdirs(const char *path);

int append_and_increment(char** dst, const char* src, size_t* dst_size);

char *build_string2(const char *s1, const char *s2);
char *build_string3(const char *s1, const char *s2, const char *s3);

int ensure_dir(const char* path, mode_t mode, uid_t uid, gid_t gid);
int ensure_media_user_dirs(const char* uuid, userid_t userid);
int ensure_config_user_dirs(userid_t userid);
int create_profile_file(const char *pkgname, gid_t gid);
void remove_profile_file(const char *pkgname);

/* commands.c */

int install(const char *uuid, const char *pkgname, uid_t uid, gid_t gid, const char *seinfo);
int uninstall(const char *uuid, const char *pkgname, userid_t userid);
int renamepkg(const char *oldpkgname, const char *newpkgname);
int fix_uid(const char *uuid, const char *pkgname, uid_t uid, gid_t gid);
int delete_user_data(const char *uuid, const char *pkgname, userid_t userid);
int make_user_data(const char *uuid, const char *pkgname, uid_t uid,
        userid_t userid, const char* seinfo);
int copy_complete_app(const char* from_uuid, const char *to_uuid,
        const char *package_name, const char *data_app_name, appid_t appid,
        const char* seinfo);
int make_user_config(userid_t userid);
int delete_user(const char *uuid, userid_t userid);
int delete_cache(const char *uuid, const char *pkgname, userid_t userid);
int delete_code_cache(const char *uuid, const char *pkgname, userid_t userid);
int move_dex(const char *src, const char *dst, const char *instruction_set);
int rm_dex(const char *path, const char *instruction_set);
int protect(char *pkgname, gid_t gid);
int get_size(const char *uuid, const char *pkgname, int userid,
        const char *apkpath, const char *libdirpath,
        const char *fwdlock_apkpath, const char *asecpath,
        const char *instruction_set, int64_t *codesize, int64_t *datasize,
        int64_t *cachesize, int64_t *asecsize);
int free_cache(const char *uuid, int64_t free_size);
int dexopt(const char *apk_path, uid_t uid, bool is_public, const char *pkgName,
           const char *instruction_set, int dexopt_needed, bool vm_safe_mode,
           bool debuggable, const char* oat_dir, bool boot_complete);
int mark_boot_complete(const char *instruction_set);
int movefiles();
int linklib(const char* uuid, const char* pkgname, const char* asecLibDir, int userId);
int idmap(const char *target_path, const char *overlay_path, uid_t uid);
int restorecon_data(const char *uuid, const char* pkgName, const char* seinfo, uid_t uid);
int create_oat_dir(const char* oat_dir, const char *instruction_set);
int rm_package_dir(const char* apk_path);
int calculate_oat_file_path(char path[PKG_PATH_MAX], const char *oat_dir, const char *apk_path,
                            const char *instruction_set);
int move_package_dir(char path[PKG_PATH_MAX], const char *oat_dir, const char *apk_path,
                            const char *instruction_set);
int link_file(const char *relative_path, const char *from_base, const char *to_base);
