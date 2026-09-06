#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "rymga_platform.h"

#include <sodium.h>

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *const OWNER_MARKER = ".rymga-owner";

static bool write_all(int file, const char *bytes, size_t length) {
    while (length > 0) {
        ssize_t written = write(file, bytes, length);
        if (written <= 0) return false;
        bytes += written; length -= (size_t)written;
    }
    return true;
}

static bool temp_name(const char *name) {
    static const char prefix[] = "rymga-loader-";
    if (strncmp(name, prefix, sizeof(prefix) - 1) != 0 || strlen(name) != sizeof(prefix) - 1 + 6) return false;
    for (const char *cursor = name + sizeof(prefix) - 1; *cursor != '\0'; cursor++)
        if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= '0' && *cursor <= '9'))) return false;
    return true;
}

static bool jar_name(const char *name) {
    size_t length = strlen(name);
    return length > 4 && strchr(name, '/') == NULL && strchr(name, '\\') == NULL &&
        name[length - 4] == '.' && (name[length - 3] == 'j' || name[length - 3] == 'J') &&
        (name[length - 2] == 'a' || name[length - 2] == 'A') && (name[length - 1] == 'r' || name[length - 1] == 'R');
}

static bool plugin_marker_name(const char *name) {
    static const char prefix[] = ".rymga-preload-", suffix[] = ".owner";
    size_t length = strlen(name), prefix_length = sizeof(prefix) - 1, suffix_length = sizeof(suffix) - 1;
    if (length != prefix_length + 32 + suffix_length || strncmp(name, prefix, prefix_length) != 0 ||
        strcmp(name + length - suffix_length, suffix) != 0) return false;
    for (size_t index = prefix_length; index < prefix_length + 32; index++)
        if (!((name[index] >= '0' && name[index] <= '9') || (name[index] >= 'a' && name[index] <= 'f'))) return false;
    return true;
}

static bool plugin_partial_name(const char *name) {
    static const char prefix[] = ".rymga-loader-";
    if (strncmp(name, prefix, sizeof(prefix) - 1) != 0 || strlen(name) != sizeof(prefix) - 1 + 6) return false;
    for (const char *cursor = name + sizeof(prefix) - 1; *cursor != '\0'; cursor++)
        if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= '0' && *cursor <= '9'))) return false;
    return true;
}

static bool file_identity_at(int directory, const char *name, uint64_t *volume, uint64_t *file) {
    struct stat info;
    if (fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(info.st_mode) || info.st_uid != geteuid()) return false;
    *volume = (uint64_t)info.st_dev; *file = (uint64_t)info.st_ino; return true;
}

static bool dead_process(long pid) {
    if (pid <= 0 || pid > INT_MAX || kill((pid_t)pid, 0) == 0) return false;
    return errno == ESRCH;
}

static bool create_plugin_marker(int directory, const char *plugin_directory, const char *filename,
                                 uint64_t volume, uint64_t file, char path[RYMGA_PLATFORM_PATH_MAX]) {
    unsigned char random[16]; char random_hex[33], filename_hex[511], marker_name[64], content[800];
    sodium_bin2hex(filename_hex, sizeof(filename_hex), (const unsigned char *)filename, strlen(filename));
    for (unsigned int attempt = 0; attempt < 128; attempt++) {
        randombytes_buf(random, sizeof(random)); sodium_bin2hex(random_hex, sizeof(random_hex), random, sizeof(random));
        if (snprintf(marker_name, sizeof(marker_name), ".rymga-preload-%s.owner", random_hex) >= (int)sizeof(marker_name) ||
            snprintf(path, RYMGA_PLATFORM_PATH_MAX, "%s/%s", plugin_directory, marker_name) >= RYMGA_PLATFORM_PATH_MAX) return false;
        int marker = openat(directory, marker_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (marker < 0) { if (errno == EEXIST) continue; return false; }
        int length = snprintf(content, sizeof(content), "RLP1 %ld %lld %llu %llu %s\n", (long)getpid(),
            (long long)time(NULL), (unsigned long long)volume, (unsigned long long)file, filename_hex);
        bool ok = length > 0 && length < (int)sizeof(content) && write_all(marker, content, (size_t)length) && fsync(marker) == 0;
        if (close(marker) != 0) ok = false;
        if (ok) return true;
        unlinkat(directory, marker_name, 0); return false;
    }
    return false;
}

static bool parse_plugin_marker(int directory, const char *marker_name, uint64_t *volume, uint64_t *file,
                                char filename[256]) {
    int marker = openat(directory, marker_name, O_RDONLY | O_NOFOLLOW); struct stat info; char content[800] = {0};
    ssize_t length = marker < 0 ? -1 : read(marker, content, sizeof(content) - 1);
    bool valid = marker >= 0 && length > 0 && fstat(marker, &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_uid == geteuid() && info.st_size == length && (info.st_mode & (S_IRWXG | S_IRWXO)) == 0;
    if (marker >= 0) close(marker);
    long pid = 0; long long created = 0; unsigned long long parsed_volume = 0, parsed_file = 0; char hex[511] = {0}, trailing = '\0';
    if (!valid || sscanf(content, "RLP1 %ld %lld %llu %llu %510s %c", &pid, &created, &parsed_volume, &parsed_file, hex, &trailing) != 5 ||
        created < 0 || !dead_process(pid)) return false;
    size_t decoded = 0;
    if (sodium_hex2bin((unsigned char *)filename, 255, hex, strlen(hex), NULL, &decoded, NULL) != 0 ||
        decoded == 0 || decoded > 255) return false;
    filename[decoded] = '\0';
    if (!jar_name(filename) || strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL) return false;
    *volume = (uint64_t)parsed_volume; *file = (uint64_t)parsed_file; return true;
}

static bool create_marker(const char *directory) {
    const char *name = strrchr(directory, '/'); name = name == NULL ? directory : name + 1;
    char path[RYMGA_PLATFORM_PATH_MAX], content[160];
    int content_length = snprintf(content, sizeof(content), "RLL1 %ld %lld %s\n", (long)getpid(), (long long)time(NULL), name);
    if (content_length <= 0 || content_length >= (int)sizeof(content) ||
        snprintf(path, sizeof(path), "%s/%s", directory, OWNER_MARKER) >= (int)sizeof(path)) return false;
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    bool ok = file >= 0 && write_all(file, content, (size_t)content_length) && fsync(file) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    if (!ok) unlink(path);
    return ok;
}

static bool stale_owned_directory(int root, const char *name, long max_age_seconds, char jar[256]) {
    int directory = openat(root, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    struct stat info;
    if (directory < 0 || fstat(directory, &info) != 0 || info.st_uid != geteuid() || (info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        if (directory >= 0) close(directory);
        return false;
    }
    int marker = openat(directory, OWNER_MARKER, O_RDONLY | O_NOFOLLOW); char content[160] = {0};
    ssize_t length = marker < 0 ? -1 : read(marker, content, sizeof(content) - 1); struct stat marker_info;
    bool valid = marker >= 0 && length > 0 && fstat(marker, &marker_info) == 0 && S_ISREG(marker_info.st_mode) &&
        marker_info.st_uid == geteuid() && marker_info.st_size == length && (marker_info.st_mode & (S_IRWXG | S_IRWXO)) == 0;
    if (marker >= 0) close(marker);
    long pid = 0; long long created = 0; char marked_name[64] = {0}, trailing = '\0';
    if (!valid || sscanf(content, "RLL1 %ld %lld %63s %c", &pid, &created, marked_name, &trailing) != 3 ||
        strcmp(marked_name, name) != 0 || pid <= 0 || pid > INT_MAX || created < 0) { close(directory); return false; }
    time_t now = time(NULL);
    if (now < 0 || created > (long long)now || (long long)now - created < max_age_seconds || kill((pid_t)pid, 0) == 0 || errno == EPERM) {
        close(directory); return false;
    }
    DIR *entries = fdopendir(directory);
    if (entries == NULL) { close(directory); return false; }
    bool safe = true; size_t jars = 0; struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, OWNER_MARKER) == 0) continue;
        struct stat file_info;
        if (fstatat(directory, entry->d_name, &file_info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(file_info.st_mode) ||
            file_info.st_uid != geteuid() || (file_info.st_mode & (S_IWGRP | S_IWOTH)) != 0) { safe = false; break; }
        if (strcmp(entry->d_name, ".artifact.part") == 0) continue;
        if (!jar_name(entry->d_name) || ++jars > 1) { safe = false; break; }
        snprintf(jar, 256, "%s", entry->d_name);
    }
    closedir(entries); return safe;
}

void rymga_platform_cleanup_stale(const char *runtime_directory, long max_age_seconds) {
    if (max_age_seconds < 1 || !rymga_platform_runtime_directory_ok(runtime_directory)) return;
    int root = open(runtime_directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root < 0) return;
    int scan_fd = dup(root); DIR *entries = scan_fd < 0 ? NULL : fdopendir(scan_fd);
    if (entries == NULL) { if (scan_fd >= 0) close(scan_fd); close(root); return; }
    struct dirent *entry; size_t checked = 0;
    while (checked < 256 && (entry = readdir(entries)) != NULL) {
        if (!temp_name(entry->d_name)) continue;
        checked++; char jar[256] = {0};
        if (!stale_owned_directory(root, entry->d_name, max_age_seconds, jar)) continue;
        int directory = openat(root, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (directory < 0) continue;
        if (jar[0] != '\0') unlinkat(directory, jar, 0);
        unlinkat(directory, ".artifact.part", 0); unlinkat(directory, OWNER_MARKER, 0); close(directory);
        unlinkat(root, entry->d_name, AT_REMOVEDIR);
    }
    closedir(entries); close(root);
}

bool rymga_platform_runtime_directory_ok(const char *path) {
    struct stat info;
    return path != NULL && lstat(path, &info) == 0 && S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode) &&
        info.st_uid == geteuid() && (info.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

bool rymga_platform_ensure_private_directory(const char *path) {
    return (mkdir(path, 0700) == 0 || errno == EEXIST) && rymga_platform_runtime_directory_ok(path);
}

bool rymga_platform_make_temp_directory(const char *runtime_directory, char *out, size_t out_size) {
    if (snprintf(out, out_size, "%s/rymga-loader-XXXXXX", runtime_directory) >= (int)out_size || mkdtemp(out) == NULL) return false;
    if (chmod(out, 0700) == 0 && create_marker(out)) return true;
    rmdir(out);
    return false;
}

bool rymga_platform_create_file(const char *path, rymga_platform_file *out) {
    *out = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    return *out >= 0;
}

bool rymga_platform_write(rymga_platform_file file, const void *bytes, size_t length) {
    const char *cursor = bytes;
    while (length > 0) {
        ssize_t written = write(file, cursor, length);
        if (written <= 0) return false;
        cursor += written;
        length -= (size_t)written;
    }
    return true;
}

bool rymga_platform_sync_close(rymga_platform_file *file) {
    bool synced = fsync(*file) == 0;
    bool closed = close(*file) == 0;
    *file = -1;
    return synced && closed;
}

void rymga_platform_close(rymga_platform_file *file) {
    if (*file >= 0) close(*file);
    *file = -1;
}

bool rymga_platform_rename(const char *from, const char *to) { return rename(from, to) == 0; }
void rymga_platform_remove_file(const char *path) { unlink(path); }
void rymga_platform_remove_directory(const char *path) {
    char marker[RYMGA_PLATFORM_PATH_MAX];
    if (snprintf(marker, sizeof(marker), "%s/%s", path, OWNER_MARKER) < (int)sizeof(marker)) unlink(marker);
    rmdir(path);
}

void rymga_platform_cleanup_plugins(const char *plugin_directory) {
    if (!rymga_platform_runtime_directory_ok(plugin_directory)) return;
    int directory = open(plugin_directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    int scan_fd = directory < 0 ? -1 : dup(directory); DIR *entries = scan_fd < 0 ? NULL : fdopendir(scan_fd);
    if (entries == NULL) { if (scan_fd >= 0) close(scan_fd); if (directory >= 0) close(directory); return; }
    struct dirent *entry; size_t checked = 0;
    while (checked < 256 && (entry = readdir(entries)) != NULL) {
        if (!plugin_marker_name(entry->d_name)) continue;
        checked++; uint64_t volume = 0, file = 0; char filename[256];
        if (!parse_plugin_marker(directory, entry->d_name, &volume, &file, filename)) continue;
        int content_fd = openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        DIR *contents = content_fd < 0 ? NULL : fdopendir(content_fd);
        if (contents == NULL) { if (content_fd >= 0) close(content_fd); continue; }
        struct dirent *candidate;
        while ((candidate = readdir(contents)) != NULL) {
            if (strcmp(candidate->d_name, filename) != 0 && !plugin_partial_name(candidate->d_name)) continue;
            uint64_t candidate_volume = 0, candidate_file = 0;
            if (file_identity_at(directory, candidate->d_name, &candidate_volume, &candidate_file) &&
                candidate_volume == volume && candidate_file == file) unlinkat(directory, candidate->d_name, 0);
        }
        closedir(contents); unlinkat(directory, entry->d_name, 0);
    }
    closedir(entries); close(directory);
}

bool rymga_platform_materialize_plugin(const char *source, const char *plugin_directory,
                                       rymga_platform_plugin *plugin) {
    if (source == NULL || plugin == NULL || !rymga_platform_runtime_directory_ok(plugin_directory)) return false;
    memset(plugin, 0, sizeof(*plugin)); rymga_platform_cleanup_plugins(plugin_directory);
    const char *filename = strrchr(source, '/'); filename = filename == NULL ? source : filename + 1;
    char partial[RYMGA_PLATFORM_PATH_MAX];
    if (snprintf(plugin->destination, sizeof(plugin->destination), "%s/%s", plugin_directory, filename) >= (int)sizeof(plugin->destination) ||
        snprintf(partial, sizeof(partial), "%s/.rymga-loader-XXXXXX", plugin_directory) >= (int)sizeof(partial)) return false;
    int directory = open(plugin_directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    int input = open(source, O_RDONLY | O_NOFOLLOW), output = mkstemp(partial);
    if (directory < 0 || input < 0 || output < 0) {
        if (directory >= 0) close(directory);
        if (input >= 0) close(input);
        if (output >= 0) { close(output); unlink(partial); } return false;
    }
    bool ok = fchmod(output, 0600) == 0; char bytes[32768];
    while (ok) {
        ssize_t count = read(input, bytes, sizeof(bytes));
        if (count == 0) break;
        if (count < 0) { if (errno == EINTR) continue; ok = false; break; }
        for (ssize_t offset = 0; offset < count;) {
            ssize_t written = write(output, bytes + offset, (size_t)(count - offset));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { ok = false; break; }
            offset += written;
        }
    }
    if (ok && fsync(output) != 0) ok = false;
    if (close(output) != 0) ok = false;
    output = -1;
    const char *partial_name = strrchr(partial, '/'); partial_name = partial_name == NULL ? partial : partial_name + 1;
    if (snprintf(plugin->anchor, sizeof(plugin->anchor), "%s", partial) >= (int)sizeof(plugin->anchor)) ok = false;
    if (ok) ok = file_identity_at(directory, partial_name, &plugin->volume_id, &plugin->file_id) &&
        create_plugin_marker(directory, plugin_directory, filename, plugin->volume_id, plugin->file_id, plugin->marker);
    if (ok && linkat(directory, partial_name, directory, filename, 0) != 0) ok = false;
    close(input);
    if (!ok) {
        unlinkat(directory, partial_name, 0);
        if (plugin->marker[0] != '\0') unlink(plugin->marker);
        uint64_t volume = 0, file = 0;
        if (file_identity_at(directory, filename, &volume, &file) && volume == plugin->volume_id && file == plugin->file_id) unlinkat(directory, filename, 0);
        memset(plugin, 0, sizeof(*plugin));
    }
    close(directory); return ok;
}

void rymga_platform_release_plugin(rymga_platform_plugin *plugin) {
    if (plugin == NULL) return;
    struct stat info;
    if (plugin->destination[0] != '\0' && lstat(plugin->destination, &info) == 0 && S_ISREG(info.st_mode) &&
        (uint64_t)info.st_dev == plugin->volume_id && (uint64_t)info.st_ino == plugin->file_id) unlink(plugin->destination);
    if (plugin->anchor[0] != '\0' && lstat(plugin->anchor, &info) == 0 && S_ISREG(info.st_mode) &&
        (uint64_t)info.st_dev == plugin->volume_id && (uint64_t)info.st_ino == plugin->file_id) unlink(plugin->anchor);
    if (plugin->marker[0] != '\0') unlink(plugin->marker);
    memset(plugin, 0, sizeof(*plugin));
}
