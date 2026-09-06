#ifdef _WIN32
#define UNICODE
#define _UNICODE
#include "rymga_loader.h"
#include "rymga_platform.h"

#include <sodium.h>
#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct { wchar_t *data; size_t length, capacity; } wide_buffer;
static HANDLE license_console = INVALID_HANDLE_VALUE;
static DWORD license_console_mode;

static BOOL WINAPI restore_license_console(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT ||
        event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT) {
        if (license_console != INVALID_HANDLE_VALUE) SetConsoleMode(license_console, license_console_mode);
    }
    return FALSE;
}

static void usage(void) {
    fputs("Usage: rymga-loader run --endpoint HTTPS_URL --product ID --channel ID --java PATH\n"
          "  (--key-id ID --public-key HEX | --trusted-key ID HEX...) [--jvm-arg ARG...]\n"
          "  [--installation-id ID] [--runtime-dir DIR] [--ca-bundle PEM] [--license-stdin] -- [app args]\n"
          "   or: rymga-loader preload --host PATH --plugin-dir DIR [same acquisition options] -- [host args]\n", stderr);
}

static char *utf8(const wchar_t *input) {
    if (input == NULL) return NULL;
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, NULL, 0, NULL, NULL);
    if (size <= 0) return NULL;
    char *output = malloc((size_t)size);
    if (output == NULL || WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, output, size, NULL, NULL) != size) {
        free(output); return NULL;
    }
    return output;
}

static wchar_t *wide(const char *input) {
    if (input == NULL) return NULL;
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, NULL, 0);
    if (size <= 0) return NULL;
    wchar_t *output = malloc((size_t)size * sizeof(*output));
    if (output == NULL || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, output, size) != size) {
        free(output); return NULL;
    }
    return output;
}

static char *default_runtime_directory(void) {
    wchar_t path[RYMGA_PLATFORM_PATH_MAX];
    DWORD length = GetTempPathW(RYMGA_PLATFORM_PATH_MAX, path);
    if (length == 0 || length + wcslen(L"rymga-loader-runtime") >= RYMGA_PLATFORM_PATH_MAX) return NULL;
    wcscpy(path + length, L"rymga-loader-runtime");
    return utf8(path);
}

static bool reserve(wide_buffer *buffer, size_t additional) {
    if (additional > 32767 - buffer->length) return false;
    size_t needed = buffer->length + additional, capacity = buffer->capacity == 0 ? 128 : buffer->capacity;
    while (capacity < needed) capacity *= 2;
    wchar_t *data = realloc(buffer->data, capacity * sizeof(*data));
    if (data == NULL) return false;
    buffer->data = data; buffer->capacity = capacity; return true;
}

static bool append_char(wide_buffer *buffer, wchar_t value) {
    if (!reserve(buffer, 1)) return false;
    buffer->data[buffer->length++] = value; return true;
}

static bool append_quoted(wide_buffer *buffer, const char *argument) {
    wchar_t *value = wide(argument);
    if (value == NULL || (buffer->length != 0 && !append_char(buffer, L' ')) || !append_char(buffer, L'\"')) {
        free(value); return false;
    }
    size_t slashes = 0;
    for (size_t index = 0;; index++) {
        wchar_t character = value[index];
        if (character == L'\\') { slashes++; continue; }
        if (character == L'\"') {
            for (size_t count = 0; count < slashes * 2 + 1; count++) if (!append_char(buffer, L'\\')) { free(value); return false; }
            if (!append_char(buffer, L'\"')) { free(value); return false; }
        } else {
            size_t count = character == L'\0' ? slashes * 2 : slashes;
            for (size_t offset = 0; offset < count; offset++) if (!append_char(buffer, L'\\')) { free(value); return false; }
            if (character == L'\0') break;
            if (!append_char(buffer, character)) { free(value); return false; }
        }
        slashes = 0;
    }
    free(value); return append_char(buffer, L'\"');
}

static wchar_t *command_line(const char *const *arguments, size_t count) {
    wide_buffer buffer = {0};
    for (size_t index = 0; index < count; index++) if (!append_quoted(&buffer, arguments[index])) { free(buffer.data); return NULL; }
    if (!append_char(&buffer, L'\0')) { free(buffer.data); return NULL; }
    return buffer.data;
}

static bool hex_key(const char *text, uint8_t output[32]) {
    if (text == NULL || strlen(text) != 64) return false;
    for (size_t index = 0; index < 32; index++) {
        char pair[3] = {text[index * 2], text[index * 2 + 1], '\0'}, *end = NULL;
        unsigned long value = strtoul(pair, &end, 16);
        if (*end != '\0' || value > 255) return false;
        output[index] = (uint8_t)value;
    }
    return true;
}

static uint8_t *read_license_stdin(size_t *length_out) {
    uint8_t *license = calloc(4097, 1); size_t length = 0; int character;
    if (license == NULL) return NULL;
    while ((character = fgetc(stdin)) != EOF && character != '\n' && character != '\r') {
        if (character == '\0' || length == 4096) { sodium_memzero(license, 4097); free(license); return NULL; }
        license[length++] = (uint8_t)character;
    }
    if (ferror(stdin) || length == 0) { sodium_memzero(license, 4097); free(license); return NULL; }
    *length_out = length; return license;
}

static uint8_t *read_license_console(size_t *length_out) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE); DWORD original;
    if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &original)) return NULL;
    license_console = input; license_console_mode = original;
    if (!SetConsoleCtrlHandler(restore_license_console, TRUE) || !SetConsoleMode(input, original & ~ENABLE_ECHO_INPUT)) {
        SetConsoleCtrlHandler(restore_license_console, FALSE); license_console = INVALID_HANDLE_VALUE; return NULL;
    }
    fputs("License: ", stderr); fflush(stderr);
    wchar_t characters[4097] = {0}; size_t length = 0; bool ok = true;
    for (;;) {
        wchar_t character; DWORD read = 0;
        if (!ReadConsoleW(input, &character, 1, &read, NULL) || read != 1) { ok = false; break; }
        if (character == L'\n' || character == L'\r') break;
        if (length == 4096) { ok = false; break; }
        characters[length++] = character;
    }
    SetConsoleMode(input, original); license_console = INVALID_HANDLE_VALUE;
    SetConsoleCtrlHandler(restore_license_console, FALSE); fputc('\n', stderr);
    int bytes = ok && length != 0 ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, characters, (int)length, NULL, 0, NULL, NULL) : 0;
    uint8_t *license = bytes > 0 && bytes <= 4096 ? calloc((size_t)bytes + 1, 1) : NULL;
    if (license != NULL && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, characters, (int)length, (char *)license, bytes, NULL, NULL) != bytes) {
        sodium_memzero(license, (size_t)bytes + 1); free(license); license = NULL;
    }
    SecureZeroMemory(characters, sizeof(characters));
    if (license != NULL) *length_out = (size_t)bytes;
    return license;
}

static BOOL WINAPI keep_parent_alive(DWORD event) {
    return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
}

int wmain(int argc, wchar_t **wide_argv) {
    char **argv = calloc((size_t)argc, sizeof(*argv));
    if (argv == NULL) return 2;
    for (int index = 0; index < argc; index++) if ((argv[index] = utf8(wide_argv[index])) == NULL) {
        for (int offset = 0; offset < argc; offset++) free(argv[offset]);
        free(argv); return 2;
    }
    bool preload = argc >= 2 && strcmp(argv[1], "preload") == 0;
    if (argc < 2 || (!preload && strcmp(argv[1], "run") != 0)) { usage(); goto usage_error; }
    const char *endpoint = NULL, *product = NULL, *channel = NULL, *program = NULL, *plugin_directory = NULL;
    const char *key_id = NULL, *key_hex = NULL, *installation_id = "local", *runtime_directory = NULL, *ca_bundle = NULL;
    const char *key_ids[16], *key_hexes[16], *jvm_args[64]; size_t key_count = 0, jvm_count = 0;
    bool license_stdin = false; int index = 2; char *default_runtime = default_runtime_directory();
    if (default_runtime == NULL) goto usage_error;
    runtime_directory = default_runtime;
    while (index < argc && strcmp(argv[index], "--") != 0) {
        if (strcmp(argv[index], "--license-stdin") == 0) { license_stdin = true; index++; continue; }
        if (strcmp(argv[index], "--trusted-key") == 0) {
            if (index + 2 >= argc || key_count == 16) { usage(); free(default_runtime); goto usage_error; }
            key_ids[key_count] = argv[index + 1]; key_hexes[key_count++] = argv[index + 2]; index += 3; continue;
        }
        if (strcmp(argv[index], "--jvm-arg") == 0) {
            if (index + 1 >= argc || jvm_count == 64) { usage(); free(default_runtime); goto usage_error; }
            jvm_args[jvm_count++] = argv[index + 1]; index += 2; continue;
        }
        if (index + 1 >= argc) { usage(); free(default_runtime); goto usage_error; }
        const char *value = argv[index + 1];
        if (strcmp(argv[index], "--endpoint") == 0) endpoint = value;
        else if (strcmp(argv[index], "--product") == 0) product = value;
        else if (strcmp(argv[index], "--channel") == 0) channel = value;
        else if (strcmp(argv[index], "--java") == 0 || strcmp(argv[index], "--host") == 0) program = value;
        else if (strcmp(argv[index], "--plugin-dir") == 0) plugin_directory = value;
        else if (strcmp(argv[index], "--key-id") == 0) key_id = value;
        else if (strcmp(argv[index], "--public-key") == 0) key_hex = value;
        else if (strcmp(argv[index], "--installation-id") == 0) installation_id = value;
        else if (strcmp(argv[index], "--runtime-dir") == 0) runtime_directory = value;
        else if (strcmp(argv[index], "--ca-bundle") == 0) ca_bundle = value;
        else { usage(); free(default_runtime); goto usage_error; }
        index += 2;
    }
    if ((key_id == NULL) != (key_hex == NULL) || (key_id != NULL && key_count == 16)) { usage(); free(default_runtime); goto usage_error; }
    if (key_id != NULL) { key_ids[key_count] = key_id; key_hexes[key_count++] = key_hex; }
    if (index == argc || endpoint == NULL || product == NULL || channel == NULL || program == NULL || key_count == 0 ||
        (preload && (plugin_directory == NULL || jvm_count != 0)) || !rymga_platform_ensure_private_directory(runtime_directory) ||
        (preload && !rymga_platform_ensure_private_directory(plugin_directory))) { usage(); free(default_runtime); goto usage_error; }
    rymga_loader_public_key keys[16] = {0};
    for (size_t key_index = 0; key_index < key_count; key_index++) {
        keys[key_index].key_id = key_ids[key_index];
        if (!hex_key(key_hexes[key_index], keys[key_index].public_key)) { fputs("Invalid Ed25519 public key.\n", stderr); free(default_runtime); goto usage_error; }
    }
    size_t license_length = 0; uint8_t *license = license_stdin ? read_license_stdin(&license_length) : read_license_console(&license_length);
    if (license == NULL) { fputs("Could not read a license.\n", stderr); free(default_runtime); goto usage_error; }
    rymga_loader_request request = {.endpoint=endpoint, .product_id=product, .channel=channel, .loader_version="0.1.0",
        .installation_id=installation_id, .runtime_directory=runtime_directory, .ca_bundle_path=ca_bundle,
        .trusted_keys=keys, .trusted_key_count=key_count, .timeout_seconds=30};
    rymga_loader_artifact *artifact = NULL; rymga_loader_result result = rymga_loader_acquire(&request, license, license_length, &artifact);
    sodium_memzero(license, license_length); free(license); sodium_memzero(keys, sizeof(keys)); free(default_runtime);
    if (result != RYMGA_LOADER_OK) { fprintf(stderr, "Could not acquire artifact: %s\n", rymga_loader_result_string(result)); goto runtime_error; }
    rymga_platform_plugin plugin = {0};
    if (preload && !rymga_platform_materialize_plugin(rymga_loader_artifact_path(artifact), plugin_directory, &plugin)) {
        fputs("Could not materialize plugin without overwriting.\n", stderr); rymga_loader_artifact_release(artifact); goto runtime_error;
    }
    size_t user_count = (size_t)(argc - index - 1), child_count = user_count + jvm_count + (preload ? 1 : 3);
    const char **child = calloc(child_count, sizeof(*child));
    if (child == NULL) { if (preload) rymga_platform_release_plugin(&plugin); rymga_loader_artifact_release(artifact); goto runtime_error; }
    child[0] = program; size_t offset = 1;
    if (!preload) {
        for (size_t argument = 0; argument < jvm_count; argument++) child[offset++] = jvm_args[argument];
        child[offset++] = "-jar"; child[offset++] = rymga_loader_artifact_path(artifact);
    }
    for (size_t argument = 0; argument < user_count; argument++) child[offset + argument] = argv[index + 1 + (int)argument];
    wchar_t *program_wide = wide(program), *command = command_line(child, child_count); free(child);
    STARTUPINFOW startup = {.cb = sizeof(startup)}; PROCESS_INFORMATION process = {0};
    SetConsoleCtrlHandler(keep_parent_alive, TRUE);
    bool started = program_wide != NULL && command != NULL && CreateProcessW(program_wide, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process);
    free(command); free(program_wide); DWORD exit_code = 1;
    if (started) {
        CloseHandle(process.hThread);
        if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hProcess);
    }
    SetConsoleCtrlHandler(keep_parent_alive, FALSE);
    if (preload) rymga_platform_release_plugin(&plugin);
    rymga_loader_artifact_release(artifact);
    for (int argument = 0; argument < argc; argument++) free(argv[argument]);
    free(argv);
    return started ? (int)exit_code : 1;
usage_error:
    for (int argument = 0; argument < argc; argument++) free(argv[argument]);
    free(argv); return 2;
runtime_error:
    for (int argument = 0; argument < argc; argument++) free(argv[argument]);
    free(argv); return 1;
}
#endif
