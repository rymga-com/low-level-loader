#define _POSIX_C_SOURCE 200809L
#include "rymga_loader.h"
#include "rymga_platform.h"

#include <sodium.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t child_pid;
static volatile sig_atomic_t license_signal;

static void forward_signal(int signal_number) {
    if (child_pid > 0) kill(-child_pid, signal_number);
}

static void interrupt_license(int signal_number) { license_signal = signal_number; }

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s run --endpoint HTTPS_URL --product ID --channel ID --java PATH\n"
                    "  (--key-id ID --public-key HEX | --trusted-key ID HEX...) [--jvm-arg ARG...]\n"
                    "  [--installation-id ID] [--runtime-dir DIR] [--ca-bundle PEM] [--license-stdin] -- [app args]\n"
                    "   or: %s preload --host PATH --plugin-dir DIR [same acquisition options] -- [host args]\n", program, program);
}

static bool hex_key(const char *text, uint8_t output[32]) {
    if (text == NULL || strlen(text) != 64) return false;
    for (size_t i = 0; i < 32; i++) {
        char pair[3] = {text[i * 2], text[i * 2 + 1], '\0'}; char *end = NULL;
        unsigned long value = strtoul(pair, &end, 16);
        if (*end != '\0' || value > 255) return false;
        output[i] = (uint8_t)value;
    }
    return true;
}

static uint8_t *read_license_tty(size_t *length_out) {
    int terminal = open("/dev/tty", O_RDWR); struct termios original, hidden;
    if (terminal < 0 || tcgetattr(terminal, &original) != 0) return NULL;
    static const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};
    struct sigaction previous[sizeof(signals) / sizeof(*signals)], temporary = {.sa_handler = interrupt_license};
    sigemptyset(&temporary.sa_mask); license_signal = 0; size_t installed = 0;
    for (; installed < sizeof(signals) / sizeof(*signals); installed++)
        if (sigaction(signals[installed], &temporary, &previous[installed]) != 0) break;
    if (installed != sizeof(signals) / sizeof(*signals)) {
        while (installed > 0) { installed--; sigaction(signals[installed], &previous[installed], NULL); }
        close(terminal); return NULL;
    }
    hidden = original; hidden.c_lflag &= (tcflag_t)~ECHO;
    if (tcsetattr(terminal, TCSAFLUSH, &hidden) != 0) {
        for (size_t index = 0; index < installed; index++) sigaction(signals[index], &previous[index], NULL);
        close(terminal); return NULL;
    }
    dprintf(terminal, "License: ");
    uint8_t *license = calloc(4097, 1); size_t length = 0; char character;
    while (license != NULL && license_signal == 0 && read(terminal, &character, 1) == 1 && character != '\n' && character != '\r') {
        if (length == 4096) { sodium_memzero(license, 4097); free(license); license = NULL; break; }
        license[length++] = (uint8_t)character;
    }
    dprintf(terminal, "\n"); tcsetattr(terminal, TCSAFLUSH, &original); close(terminal);
    for (size_t index = 0; index < installed; index++) sigaction(signals[index], &previous[index], NULL);
    int interrupted = license_signal; license_signal = 0;
    if (interrupted != 0) {
        if (license != NULL) { sodium_memzero(license, 4097); free(license); }
        raise(interrupted); return NULL;
    }
    if (license == NULL || length == 0) { if (license != NULL) { sodium_memzero(license, 4097); free(license); } return NULL; }
    *length_out = length; return license;
}

static uint8_t *read_license_stdin(size_t *length_out) {
    uint8_t *license = calloc(4097, 1); if (license == NULL) return NULL;
    size_t length = 0; int character;
    while ((character = fgetc(stdin)) != EOF && character != '\n' && character != '\r') {
        if (character == '\0' || length == 4096) {
            sodium_memzero(license, 4097); free(license); return NULL;
        }
        license[length++] = (uint8_t)character;
    }
    if (ferror(stdin) || length == 0) { sodium_memzero(license, 4097); free(license); return NULL; }
    *length_out = length; return license;
}

int main(int argc, char **argv) {
    bool preload = argc >= 2 && strcmp(argv[1], "preload") == 0;
    if (argc < 2 || (!preload && strcmp(argv[1], "run") != 0)) { usage(argv[0]); return 2; }
    const char *endpoint = NULL, *product = NULL, *channel = NULL, *java = NULL, *plugin_directory = NULL, *key_id = NULL, *key_hex = NULL, *installation_id = "local";
    const char *runtime_dir = "/tmp/rymga-loader-runtime", *ca_bundle = NULL, *key_ids[16], *key_hexes[16], *jvm_args[64];
    size_t key_count = 0, jvm_count = 0; bool license_stdin = false; int index = 2;
    while (index < argc && strcmp(argv[index], "--") != 0) {
        if (strcmp(argv[index], "--license-stdin") == 0) { license_stdin = true; index++; continue; }
        if (strcmp(argv[index], "--trusted-key") == 0) {
            if (index + 2 >= argc || key_count == 16) { usage(argv[0]); return 2; }
            key_ids[key_count] = argv[index + 1]; key_hexes[key_count++] = argv[index + 2]; index += 3; continue;
        }
        if (strcmp(argv[index], "--jvm-arg") == 0) {
            if (index + 1 >= argc || jvm_count == 64) { usage(argv[0]); return 2; }
            jvm_args[jvm_count++] = argv[index + 1]; index += 2; continue;
        }
        if (index + 1 >= argc) { usage(argv[0]); return 2; }
        const char *value = argv[index + 1];
        if (strcmp(argv[index], "--endpoint") == 0) endpoint = value;
        else if (strcmp(argv[index], "--product") == 0) product = value;
        else if (strcmp(argv[index], "--channel") == 0) channel = value;
        else if (strcmp(argv[index], "--java") == 0) java = value;
        else if (strcmp(argv[index], "--host") == 0) java = value;
        else if (strcmp(argv[index], "--plugin-dir") == 0) plugin_directory = value;
        else if (strcmp(argv[index], "--key-id") == 0) key_id = value;
        else if (strcmp(argv[index], "--public-key") == 0) key_hex = value;
        else if (strcmp(argv[index], "--installation-id") == 0) installation_id = value;
        else if (strcmp(argv[index], "--runtime-dir") == 0) runtime_dir = value;
        else if (strcmp(argv[index], "--ca-bundle") == 0) ca_bundle = value;
        else { usage(argv[0]); return 2; }
        index += 2;
    }
    if ((key_id == NULL) != (key_hex == NULL) || (key_id != NULL && key_count == 16)) { usage(argv[0]); return 2; }
    if (key_id != NULL) { key_ids[key_count] = key_id; key_hexes[key_count++] = key_hex; }
    if (index == argc || endpoint == NULL || product == NULL || channel == NULL || java == NULL || key_count == 0 ||
        (preload && (plugin_directory == NULL || jvm_count != 0)) ||
        !rymga_platform_ensure_private_directory(runtime_dir) ||
        (preload && !rymga_platform_ensure_private_directory(plugin_directory))) { usage(argv[0]); return 2; }

    rymga_loader_public_key keys[16] = {0};
    for (size_t key_index = 0; key_index < key_count; key_index++) {
        keys[key_index].key_id = key_ids[key_index];
        if (!hex_key(key_hexes[key_index], keys[key_index].public_key)) { fputs("Invalid Ed25519 public key.\n", stderr); return 2; }
    }
    size_t license_length = 0; uint8_t *license = license_stdin ? read_license_stdin(&license_length) : read_license_tty(&license_length);
    if (license == NULL) { fputs("Could not read a license.\n", stderr); return 2; }
    rymga_loader_request request = { .endpoint = endpoint, .product_id = product, .channel = channel, .loader_version = "0.1.0",
        .installation_id = installation_id, .runtime_directory = runtime_dir, .ca_bundle_path = ca_bundle, .trusted_keys = keys, .trusted_key_count = key_count, .timeout_seconds = 30 };
    rymga_loader_artifact *artifact = NULL; rymga_loader_result result = rymga_loader_acquire(&request, license, license_length, &artifact);
    sodium_memzero(license, license_length); free(license); sodium_memzero(keys, sizeof(keys));
    if (result != RYMGA_LOADER_OK) { fprintf(stderr, "Could not acquire artifact: %s\n", rymga_loader_result_string(result)); return 1; }

    rymga_platform_plugin plugin = {0};
    if (preload && !rymga_platform_materialize_plugin(rymga_loader_artifact_path(artifact), plugin_directory, &plugin)) { fputs("Could not materialize plugin without overwriting.\n", stderr); rymga_loader_artifact_release(artifact); return 1; }
    size_t app_argc = (size_t)(argc - index - 1); char **child_argv = calloc(app_argc + jvm_count + (preload ? 2 : 4), sizeof(*child_argv));
    if (child_argv == NULL) { if (preload) rymga_platform_release_plugin(&plugin); rymga_loader_artifact_release(artifact); return 1; }
    child_argv[0] = (char *)java;
    size_t offset = 1;
    if (!preload) {
        for (size_t argument = 0; argument < jvm_count; argument++) child_argv[offset++] = (char *)jvm_args[argument];
        child_argv[offset++] = "-jar"; child_argv[offset++] = (char *)rymga_loader_artifact_path(artifact);
    }
    for (size_t i = 0; i < app_argc; i++) child_argv[i + offset] = argv[index + 1 + (int)i];
    struct sigaction action = { .sa_handler = forward_signal }; sigemptyset(&action.sa_mask); sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
    pid_t pid = fork();
    if (pid == 0) { setpgid(0, 0); execv(java, child_argv); _exit(127); }
    if (pid < 0) { free(child_argv); if (preload) rymga_platform_release_plugin(&plugin); rymga_loader_artifact_release(artifact); return 1; }
    child_pid = pid; int status = 1; pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    child_pid = 0; free(child_argv); if (preload) rymga_platform_release_plugin(&plugin); rymga_loader_artifact_release(artifact);
    if (waited != pid) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}
