#ifndef RYMGA_PLATFORM_H
#define RYMGA_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#define RYMGA_PLATFORM_PATH_MAX 32768
typedef HANDLE rymga_platform_file;
#else
#include <limits.h>
#define RYMGA_PLATFORM_PATH_MAX PATH_MAX
typedef int rymga_platform_file;
#endif

typedef struct {
    char destination[RYMGA_PLATFORM_PATH_MAX];
    char marker[RYMGA_PLATFORM_PATH_MAX];
    char anchor[RYMGA_PLATFORM_PATH_MAX];
    uint64_t volume_id;
    uint64_t file_id;
} rymga_platform_plugin;

bool rymga_platform_runtime_directory_ok(const char *path);
bool rymga_platform_ensure_private_directory(const char *path);
void rymga_platform_cleanup_stale(const char *runtime_directory, long max_age_seconds);
bool rymga_platform_make_temp_directory(const char *runtime_directory, char *out, size_t out_size);
bool rymga_platform_create_file(const char *path, rymga_platform_file *out);
bool rymga_platform_write(rymga_platform_file file, const void *bytes, size_t length);
bool rymga_platform_sync_close(rymga_platform_file *file);
void rymga_platform_close(rymga_platform_file *file);
bool rymga_platform_rename(const char *from, const char *to);
void rymga_platform_remove_file(const char *path);
void rymga_platform_remove_directory(const char *path);
void rymga_platform_cleanup_plugins(const char *plugin_directory);
bool rymga_platform_materialize_plugin(const char *source, const char *plugin_directory,
                                       rymga_platform_plugin *plugin);
void rymga_platform_release_plugin(rymga_platform_plugin *plugin);

#endif
