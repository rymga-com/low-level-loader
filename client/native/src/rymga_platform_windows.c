#ifdef _WIN32
#include "rymga_platform.h"

#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void randombytes_buf(void *buf, size_t size);
static const char *const OWNER_MARKER = ".rymga-owner";

static bool wide(const char *input, wchar_t output[RYMGA_PLATFORM_PATH_MAX]) {
    if (input == NULL) return false;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, output, RYMGA_PLATFORM_PATH_MAX);
    return count > 0 && count < RYMGA_PLATFORM_PATH_MAX;
}

static bool temp_name(const wchar_t *name) {
    static const wchar_t prefix[] = L"rymga-loader-";
    if (wcsncmp(name, prefix, sizeof(prefix) / sizeof(*prefix) - 1) != 0 || wcslen(name) != sizeof(prefix) / sizeof(*prefix) - 1 + 32) return false;
    for (const wchar_t *cursor = name + sizeof(prefix) / sizeof(*prefix) - 1; *cursor != L'\0'; cursor++)
        if (!((*cursor >= L'a' && *cursor <= L'f') || (*cursor >= L'0' && *cursor <= L'9'))) return false;
    return true;
}

static bool jar_name(const wchar_t *name) {
    size_t length = wcslen(name);
    return length > 4 && wcschr(name, L'/') == NULL && wcschr(name, L'\\') == NULL && _wcsicmp(name + length - 4, L".jar") == 0;
}

static bool plugin_marker_name(const wchar_t *name) {
    static const wchar_t prefix[] = L".rymga-preload-", suffix[] = L".owner";
    size_t length = wcslen(name), prefix_length = sizeof(prefix) / sizeof(*prefix) - 1,
        suffix_length = sizeof(suffix) / sizeof(*suffix) - 1;
    if (length != prefix_length + 32 + suffix_length || wcsncmp(name, prefix, prefix_length) != 0 ||
        wcscmp(name + length - suffix_length, suffix) != 0) return false;
    for (size_t index = prefix_length; index < prefix_length + 32; index++)
        if (!((name[index] >= L'0' && name[index] <= L'9') || (name[index] >= L'a' && name[index] <= L'f'))) return false;
    return true;
}

static bool plugin_partial_name(const wchar_t *name) {
    static const wchar_t prefix[] = L".rymga-loader-";
    if (wcsncmp(name, prefix, sizeof(prefix) / sizeof(*prefix) - 1) != 0 ||
        wcslen(name) != sizeof(prefix) / sizeof(*prefix) - 1 + 32 + 5) return false;
    const wchar_t *cursor = name + sizeof(prefix) / sizeof(*prefix) - 1;
    for (size_t index = 0; index < 32; index++)
        if (!((cursor[index] >= L'0' && cursor[index] <= L'9') || (cursor[index] >= L'a' && cursor[index] <= L'f'))) return false;
    return wcscmp(cursor + 32, L".part") == 0;
}

static void hex_encode(const unsigned char *input, size_t length, char *output) {
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0; index < length; index++) {
        output[index * 2] = digits[input[index] >> 4]; output[index * 2 + 1] = digits[input[index] & 15];
    }
    output[length * 2] = '\0';
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool hex_decode(const char *input, char output[256]) {
    size_t length = strlen(input);
    if (length == 0 || length > 510 || (length & 1) != 0) return false;
    for (size_t index = 0; index < length / 2; index++) {
        int high = hex_digit(input[index * 2]), low = hex_digit(input[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[index] = (char)((high << 4) | low);
    }
    output[length / 2] = '\0'; return true;
}

static bool file_identity(const char *path, uint64_t *volume, uint64_t *file) {
    wchar_t value[RYMGA_PLATFORM_PATH_MAX];
    if (!wide(path, value)) return false;
    HANDLE handle = CreateFileW(value, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION info;
    bool ok = handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle, &info) &&
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
    if (ok) {
        *volume = info.dwVolumeSerialNumber;
        *file = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    }
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    return ok;
}

static bool create_marker(const char *directory) {
    const char *forward = strrchr(directory, '/'), *backward = strrchr(directory, '\\'), *name = forward;
    if (backward != NULL && (name == NULL || backward > name)) name = backward;
    name = name == NULL ? directory : name + 1;
    char path[RYMGA_PLATFORM_PATH_MAX], content[160]; wchar_t path_wide[RYMGA_PLATFORM_PATH_MAX];
    int length = snprintf(content, sizeof(content), "RLL1 %lu %lld %s\n", (unsigned long)GetCurrentProcessId(), (long long)time(NULL), name);
    if (length <= 0 || length >= (int)sizeof(content) || snprintf(path, sizeof(path), "%s/%s", directory, OWNER_MARKER) >= (int)sizeof(path) || !wide(path, path_wide)) return false;
    HANDLE file = CreateFileW(path_wide, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    DWORD written = 0; bool ok = file != INVALID_HANDLE_VALUE && WriteFile(file, content, (DWORD)length, &written, NULL) &&
        written == (DWORD)length && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE && !CloseHandle(file)) ok = false;
    if (!ok) DeleteFileW(path_wide);
    return ok;
}

static bool process_is_alive(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == NULL) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD exit_code = 0; bool alive = !GetExitCodeProcess(process, &exit_code) || exit_code == STILL_ACTIVE;
    CloseHandle(process); return alive;
}

static bool parse_plugin_marker(const char *path, uint64_t *volume, uint64_t *file, char filename[256]) {
    wchar_t value[RYMGA_PLATFORM_PATH_MAX];
    if (!wide(path, value)) return false;
    HANDLE marker = CreateFileW(value, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION info; LARGE_INTEGER size; char content[800] = {0}; DWORD length = 0;
    bool valid = marker != INVALID_HANDLE_VALUE && GetFileInformationByHandle(marker, &info) &&
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
        GetFileSizeEx(marker, &size) && size.QuadPart > 0 && size.QuadPart < (LONGLONG)sizeof(content) &&
        ReadFile(marker, content, (DWORD)size.QuadPart, &length, NULL) && length == (DWORD)size.QuadPart;
    if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
    unsigned long pid = 0; long long created = 0; unsigned long long parsed_volume = 0, parsed_file = 0;
    char hex[511] = {0}, trailing = '\0';
    if (!valid || sscanf(content, "RLP1 %lu %lld %llu %llu %510s %c", &pid, &created, &parsed_volume, &parsed_file, hex, &trailing) != 5 ||
        pid == 0 || created < 0 || process_is_alive((DWORD)pid) || !hex_decode(hex, filename)) return false;
    wchar_t *filename_wide = NULL; int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, NULL, 0);
    if (count > 0) filename_wide = LocalAlloc(LPTR, (size_t)count * sizeof(*filename_wide));
    bool safe = filename_wide != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, filename_wide, count) == count && jar_name(filename_wide);
    if (filename_wide != NULL) LocalFree(filename_wide);
    if (!safe) return false;
    *volume = (uint64_t)parsed_volume; *file = (uint64_t)parsed_file; return true;
}

static bool create_plugin_marker(const char *plugin_directory, const char *filename, uint64_t volume,
                                 uint64_t file, char path[RYMGA_PLATFORM_PATH_MAX]) {
    unsigned char random[16]; char random_hex[33], filename_hex[511], content[800];
    hex_encode((const unsigned char *)filename, strlen(filename), filename_hex);
    for (unsigned int attempt = 0; attempt < 128; attempt++) {
        randombytes_buf(random, sizeof(random)); hex_encode(random, sizeof(random), random_hex);
        if (snprintf(path, RYMGA_PLATFORM_PATH_MAX, "%s/.rymga-preload-%s.owner", plugin_directory, random_hex) >= RYMGA_PLATFORM_PATH_MAX) return false;
        wchar_t value[RYMGA_PLATFORM_PATH_MAX];
        if (!wide(path, value)) return false;
        HANDLE marker = CreateFileW(value, GENERIC_WRITE, 0, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (marker == INVALID_HANDLE_VALUE) { if (GetLastError() == ERROR_FILE_EXISTS) continue; return false; }
        int content_length = snprintf(content, sizeof(content), "RLP1 %lu %lld %llu %llu %s\n",
            (unsigned long)GetCurrentProcessId(), (long long)time(NULL), (unsigned long long)volume,
            (unsigned long long)file, filename_hex);
        DWORD written = 0; bool ok = content_length > 0 && content_length < (int)sizeof(content) &&
            WriteFile(marker, content, (DWORD)content_length, &written, NULL) && written == (DWORD)content_length && FlushFileBuffers(marker);
        if (!CloseHandle(marker)) ok = false;
        if (ok) return true;
        DeleteFileW(value); return false;
    }
    return false;
}

static bool stale_owned_directory(const char *path, const char *name, long max_age_seconds, char jar[256]) {
    if (!rymga_platform_runtime_directory_ok(path)) return false;
    char marker_path[RYMGA_PLATFORM_PATH_MAX], pattern_path[RYMGA_PLATFORM_PATH_MAX];
    wchar_t marker_wide[RYMGA_PLATFORM_PATH_MAX], pattern[RYMGA_PLATFORM_PATH_MAX];
    if (snprintf(marker_path, sizeof(marker_path), "%s/%s", path, OWNER_MARKER) >= (int)sizeof(marker_path) || !wide(marker_path, marker_wide) ||
        snprintf(pattern_path, sizeof(pattern_path), "%s/*", path) >= (int)sizeof(pattern_path) || !wide(pattern_path, pattern)) return false;
    HANDLE marker = CreateFileW(marker_wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION marker_info; LARGE_INTEGER marker_size; char content[160] = {0}; DWORD length = 0;
    bool valid = marker != INVALID_HANDLE_VALUE && GetFileInformationByHandle(marker, &marker_info) &&
        (marker_info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
        GetFileSizeEx(marker, &marker_size) && marker_size.QuadPart > 0 && marker_size.QuadPart < (LONGLONG)sizeof(content) &&
        ReadFile(marker, content, (DWORD)marker_size.QuadPart, &length, NULL) && length == (DWORD)marker_size.QuadPart;
    if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
    unsigned long pid = 0; long long created = 0; char marked_name[64] = {0}, trailing = '\0';
    if (!valid || sscanf(content, "RLL1 %lu %lld %63s %c", &pid, &created, marked_name, &trailing) != 3 ||
        strcmp(marked_name, name) != 0 || pid == 0 || created < 0) return false;
    time_t now = time(NULL);
    if (now < 0 || created > (long long)now || (long long)now - created < max_age_seconds || process_is_alive((DWORD)pid)) return false;
    WIN32_FIND_DATAW entry; HANDLE search = FindFirstFileW(pattern, &entry); size_t jars = 0; bool safe = search != INVALID_HANDLE_VALUE;
    while (safe) {
        if (wcscmp(entry.cFileName, L".") != 0 && wcscmp(entry.cFileName, L"..") != 0 && wcscmp(entry.cFileName, L".rymga-owner") != 0) {
            if ((entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) safe = false;
            else if (wcscmp(entry.cFileName, L".artifact.part") != 0 &&
                (!jar_name(entry.cFileName) || ++jars > 1 || WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, entry.cFileName, -1, jar, 256, NULL, NULL) <= 0)) safe = false;
        }
        if (!safe || !FindNextFileW(search, &entry)) break;
    }
    DWORD search_error = GetLastError();
    if (search != INVALID_HANDLE_VALUE) FindClose(search);
    return safe && search_error == ERROR_NO_MORE_FILES;
}

void rymga_platform_cleanup_stale(const char *runtime_directory, long max_age_seconds) {
    if (max_age_seconds < 1 || !rymga_platform_runtime_directory_ok(runtime_directory)) return;
    char pattern_path[RYMGA_PLATFORM_PATH_MAX]; wchar_t pattern[RYMGA_PLATFORM_PATH_MAX];
    if (snprintf(pattern_path, sizeof(pattern_path), "%s/*", runtime_directory) >= (int)sizeof(pattern_path) || !wide(pattern_path, pattern)) return;
    WIN32_FIND_DATAW entry; HANDLE search = FindFirstFileW(pattern, &entry); size_t checked = 0;
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (checked >= 256) break;
        if (!temp_name(entry.cFileName) || (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != FILE_ATTRIBUTE_DIRECTORY) continue;
        checked++; char name[64], path[RYMGA_PLATFORM_PATH_MAX], jar[256] = {0}, file[RYMGA_PLATFORM_PATH_MAX];
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, entry.cFileName, -1, name, sizeof(name), NULL, NULL) <= 0 ||
            snprintf(path, sizeof(path), "%s/%s", runtime_directory, name) >= (int)sizeof(path) || !stale_owned_directory(path, name, max_age_seconds, jar)) continue;
        if (jar[0] != '\0' && snprintf(file, sizeof(file), "%s/%s", path, jar) < (int)sizeof(file)) rymga_platform_remove_file(file);
        if (snprintf(file, sizeof(file), "%s/.artifact.part", path) < (int)sizeof(file)) rymga_platform_remove_file(file);
        rymga_platform_remove_directory(path);
    } while (FindNextFileW(search, &entry));
    FindClose(search);
}

static bool unsafe_write_acl(PACL acl, PSID owner) {
    if (acl == NULL) return true;
    BYTE system_buffer[SECURITY_MAX_SID_SIZE], administrators_buffer[SECURITY_MAX_SID_SIZE];
    DWORD system_size = sizeof(system_buffer), administrators_size = sizeof(administrators_buffer);
    PSID system = system_buffer, administrators = administrators_buffer;
    if (!CreateWellKnownSid(WinLocalSystemSid, NULL, system, &system_size) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, administrators, &administrators_size)) return true;
    ACL_SIZE_INFORMATION info;
    if (!GetAclInformation(acl, &info, sizeof(info), AclSizeInformation)) return true;
    const DWORD writable = GENERIC_ALL | GENERIC_WRITE | FILE_GENERIC_WRITE | DELETE | WRITE_DAC | WRITE_OWNER;
    for (DWORD index = 0; index < info.AceCount; index++) {
        void *raw = NULL;
        if (!GetAce(acl, index, &raw)) return true;
        ACE_HEADER *header = raw;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE || header->AceType == ACCESS_DENIED_OBJECT_ACE_TYPE ||
            header->AceType == ACCESS_DENIED_CALLBACK_ACE_TYPE || header->AceType == ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return true;
        ACCESS_ALLOWED_ACE *ace = raw;
        PSID trustee = &ace->SidStart;
        if ((ace->Mask & writable) != 0 && !EqualSid(trustee, owner) && !EqualSid(trustee, system) && !EqualSid(trustee, administrators)) return true;
    }
    return false;
}

bool rymga_platform_ensure_private_directory(const char *path) {
    wchar_t value[RYMGA_PLATFORM_PATH_MAX];
    if (!wide(path, value)) return false;
    HANDLE token = NULL; DWORD needed = 0; TOKEN_USER *user = NULL;
    bool ready = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
        GetTokenInformation(token, TokenUser, NULL, 0, &needed) == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER;
    if (ready) {
        user = LocalAlloc(LPTR, needed);
        ready = user != NULL && GetTokenInformation(token, TokenUser, user, needed, &needed);
    }
    LPWSTR sid = NULL; PSECURITY_DESCRIPTOR descriptor = NULL;
    wchar_t sddl[512];
    if (ready) {
        int written = ConvertSidToStringSidW(user->User.Sid, &sid) ?
            swprintf(sddl, sizeof(sddl) / sizeof(*sddl), L"D:P(A;;FA;;;%ls)(A;;FA;;;SY)(A;;FA;;;BA)", sid) : -1;
        ready = written > 0 && (size_t)written < sizeof(sddl) / sizeof(*sddl) &&
            ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, NULL);
    }
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor, FALSE};
    bool created = false; DWORD error = ERROR_ACCESS_DENIED;
    if (ready) { created = CreateDirectoryW(value, &attributes); error = created ? ERROR_SUCCESS : GetLastError(); }
    if (descriptor != NULL) LocalFree(descriptor);
    if (sid != NULL) LocalFree(sid);
    if (user != NULL) LocalFree(user);
    if (token != NULL) CloseHandle(token);
    return (created || error == ERROR_ALREADY_EXISTS) && rymga_platform_runtime_directory_ok(path);
}

bool rymga_platform_runtime_directory_ok(const char *path) {
    wchar_t value[RYMGA_PLATFORM_PATH_MAX];
    if (!wide(path, value)) return false;
    DWORD attributes = GetFileAttributesW(value);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != FILE_ATTRIBUTE_DIRECTORY) return false;
    PSID owner = NULL; PACL dacl = NULL; PSECURITY_DESCRIPTOR descriptor = NULL;
    DWORD status = GetNamedSecurityInfoW(value, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &owner, NULL, &dacl, NULL, &descriptor);
    HANDLE token = NULL; DWORD needed = 0; TOKEN_USER *user = NULL;
    bool ok = status == ERROR_SUCCESS && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
        GetTokenInformation(token, TokenUser, NULL, 0, &needed) == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER;
    if (ok) {
        user = LocalAlloc(LPTR, needed);
        ok = user != NULL && GetTokenInformation(token, TokenUser, user, needed, &needed) && EqualSid(owner, user->User.Sid) && !unsafe_write_acl(dacl, owner);
    }
    if (user != NULL) LocalFree(user);
    if (token != NULL) CloseHandle(token);
    if (descriptor != NULL) LocalFree(descriptor);
    return ok;
}

bool rymga_platform_make_temp_directory(const char *runtime_directory, char *out, size_t out_size) {
    unsigned char random[16];
    for (unsigned int attempt = 0; attempt < 128; attempt++) {
        randombytes_buf(random, sizeof(random));
        char suffix[33];
        for (size_t i = 0; i < sizeof(random); i++) snprintf(suffix + i * 2, 3, "%02x", random[i]);
        if (snprintf(out, out_size, "%s/rymga-loader-%s", runtime_directory, suffix) >= (int)out_size) return false;
        wchar_t value[RYMGA_PLATFORM_PATH_MAX];
        if (!wide(out, value)) return false;
        if (CreateDirectoryW(value, NULL)) {
            if (rymga_platform_runtime_directory_ok(out) && create_marker(out)) return true;
            RemoveDirectoryW(value);
            return false;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    return false;
}

bool rymga_platform_create_file(const char *path, rymga_platform_file *out) {
    wchar_t value[RYMGA_PLATFORM_PATH_MAX];
    if (!wide(path, value)) return false;
    *out = CreateFileW(value, GENERIC_WRITE, 0, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    return *out != INVALID_HANDLE_VALUE;
}

bool rymga_platform_write(rymga_platform_file file, const void *bytes, size_t length) {
    const BYTE *cursor = bytes;
    while (length > 0) {
        DWORD part = length > MAXDWORD ? MAXDWORD : (DWORD)length, written = 0;
        if (!WriteFile(file, cursor, part, &written, NULL) || written == 0) return false;
        cursor += written;
        length -= written;
    }
    return true;
}

bool rymga_platform_sync_close(rymga_platform_file *file) {
    bool flushed = FlushFileBuffers(*file);
    bool closed = CloseHandle(*file);
    *file = INVALID_HANDLE_VALUE;
    return flushed && closed;
}

void rymga_platform_close(rymga_platform_file *file) {
    if (*file != INVALID_HANDLE_VALUE) CloseHandle(*file);
    *file = INVALID_HANDLE_VALUE;
}

bool rymga_platform_rename(const char *from, const char *to) {
    wchar_t source[RYMGA_PLATFORM_PATH_MAX], target[RYMGA_PLATFORM_PATH_MAX];
    return wide(from, source) && wide(to, target) && MoveFileExW(source, target, MOVEFILE_WRITE_THROUGH);
}

void rymga_platform_remove_file(const char *path) {
    wchar_t value[RYMGA_PLATFORM_PATH_MAX]; if (wide(path, value)) DeleteFileW(value);
}

void rymga_platform_remove_directory(const char *path) {
    char marker[RYMGA_PLATFORM_PATH_MAX]; wchar_t value[RYMGA_PLATFORM_PATH_MAX];
    if (snprintf(marker, sizeof(marker), "%s/%s", path, OWNER_MARKER) < (int)sizeof(marker)) rymga_platform_remove_file(marker);
    if (wide(path, value)) RemoveDirectoryW(value);
}

void rymga_platform_cleanup_plugins(const char *plugin_directory) {
    if (!rymga_platform_runtime_directory_ok(plugin_directory)) return;
    char pattern_path[RYMGA_PLATFORM_PATH_MAX]; wchar_t pattern[RYMGA_PLATFORM_PATH_MAX];
    if (snprintf(pattern_path, sizeof(pattern_path), "%s/*", plugin_directory) >= (int)sizeof(pattern_path) || !wide(pattern_path, pattern)) return;
    WIN32_FIND_DATAW entry; HANDLE search = FindFirstFileW(pattern, &entry); size_t checked = 0;
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (checked >= 256) break;
        if (!plugin_marker_name(entry.cFileName) || (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) continue;
        checked++; char marker_name[64], marker_path[RYMGA_PLATFORM_PATH_MAX], filename[256]; uint64_t volume = 0, file = 0;
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, entry.cFileName, -1, marker_name, sizeof(marker_name), NULL, NULL) <= 0 ||
            snprintf(marker_path, sizeof(marker_path), "%s/%s", plugin_directory, marker_name) >= (int)sizeof(marker_path) ||
            !parse_plugin_marker(marker_path, &volume, &file, filename)) continue;
        wchar_t filename_wide[256];
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, filename_wide, 256) > 0) {
            char destination[RYMGA_PLATFORM_PATH_MAX]; uint64_t candidate_volume = 0, candidate_file = 0;
            if (snprintf(destination, sizeof(destination), "%s/%s", plugin_directory, filename) < (int)sizeof(destination) &&
                file_identity(destination, &candidate_volume, &candidate_file) && candidate_volume == volume && candidate_file == file)
                rymga_platform_remove_file(destination);
        }
        WIN32_FIND_DATAW candidate; HANDLE contents = FindFirstFileW(pattern, &candidate);
        if (contents != INVALID_HANDLE_VALUE) {
            do {
                if (!plugin_partial_name(candidate.cFileName)) continue;
                char partial_name[64], partial[RYMGA_PLATFORM_PATH_MAX]; uint64_t candidate_volume = 0, candidate_file = 0;
                if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, candidate.cFileName, -1, partial_name, sizeof(partial_name), NULL, NULL) > 0 &&
                    snprintf(partial, sizeof(partial), "%s/%s", plugin_directory, partial_name) < (int)sizeof(partial) &&
                    file_identity(partial, &candidate_volume, &candidate_file) && candidate_volume == volume && candidate_file == file)
                    rymga_platform_remove_file(partial);
            } while (FindNextFileW(contents, &candidate));
            FindClose(contents);
        }
        rymga_platform_remove_file(marker_path);
    } while (FindNextFileW(search, &entry));
    FindClose(search);
}

bool rymga_platform_materialize_plugin(const char *source, const char *plugin_directory,
                                       rymga_platform_plugin *plugin) {
    if (source == NULL || plugin == NULL || !rymga_platform_runtime_directory_ok(plugin_directory)) return false;
    memset(plugin, 0, sizeof(*plugin)); rymga_platform_cleanup_plugins(plugin_directory);
    const char *forward = strrchr(source, '/'), *backward = strrchr(source, '\\'), *filename = forward;
    if (backward != NULL && (filename == NULL || backward > filename)) filename = backward;
    filename = filename == NULL ? source : filename + 1;
    unsigned char random[16]; char random_hex[33], partial[RYMGA_PLATFORM_PATH_MAX]; randombytes_buf(random, sizeof(random));
    hex_encode(random, sizeof(random), random_hex);
    if (snprintf(plugin->destination, sizeof(plugin->destination), "%s/%s", plugin_directory, filename) >= (int)sizeof(plugin->destination) ||
        snprintf(partial, sizeof(partial), "%s/.rymga-loader-%s.part", plugin_directory, random_hex) >= (int)sizeof(partial) ||
        snprintf(plugin->anchor, sizeof(plugin->anchor), "%s", partial) >= (int)sizeof(plugin->anchor)) return false;
    wchar_t source_wide[RYMGA_PLATFORM_PATH_MAX], partial_wide[RYMGA_PLATFORM_PATH_MAX], destination_wide[RYMGA_PLATFORM_PATH_MAX];
    if (!wide(source, source_wide) || !wide(partial, partial_wide) || !wide(plugin->destination, destination_wide) ||
        !CopyFileW(source_wide, partial_wide, TRUE)) return false;
    bool ok = file_identity(partial, &plugin->volume_id, &plugin->file_id) &&
        create_plugin_marker(plugin_directory, filename, plugin->volume_id, plugin->file_id, plugin->marker) &&
        CreateHardLinkW(destination_wide, partial_wide, NULL);
    if (!ok) {
        DeleteFileW(partial_wide);
        if (plugin->marker[0] != '\0') rymga_platform_remove_file(plugin->marker);
        uint64_t volume = 0, file = 0;
        if (file_identity(plugin->destination, &volume, &file) && volume == plugin->volume_id && file == plugin->file_id)
            rymga_platform_remove_file(plugin->destination);
        memset(plugin, 0, sizeof(*plugin));
    }
    return ok;
}

void rymga_platform_release_plugin(rymga_platform_plugin *plugin) {
    if (plugin == NULL) return;
    uint64_t volume = 0, file = 0;
    if (plugin->destination[0] != '\0' && file_identity(plugin->destination, &volume, &file) &&
        volume == plugin->volume_id && file == plugin->file_id) rymga_platform_remove_file(plugin->destination);
    if (plugin->anchor[0] != '\0' && file_identity(plugin->anchor, &volume, &file) &&
        volume == plugin->volume_id && file == plugin->file_id) rymga_platform_remove_file(plugin->anchor);
    if (plugin->marker[0] != '\0') rymga_platform_remove_file(plugin->marker);
    SecureZeroMemory(plugin, sizeof(*plugin));
}
#endif
