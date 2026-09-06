#include "rymga_loader.h"
#include "rymga_platform.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void mark_stale(const char *directory) {
    const char *name = strrchr(directory, '/'); name = name == NULL ? directory : name + 1;
    char marker[512]; assert(snprintf(marker, sizeof(marker), "%s/.rymga-owner", directory) < (int)sizeof(marker));
    FILE *file = fopen(marker, "wb"); assert(file != NULL);
    assert(fprintf(file, "RLL1 2147483647 1 %s\n", name) > 0);
    assert(fclose(file) == 0);
}

int main(void) {
    assert(strcmp(rymga_loader_result_string(RYMGA_LOADER_SIGNATURE_ERROR), "signature error") == 0);
    assert(rymga_loader_acquire(NULL, NULL, 0, NULL) == RYMGA_LOADER_INVALID_ARGUMENT);
    char directory[] = "/tmp/rymga-loader-native-test-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    assert(rymga_platform_ensure_private_directory(directory));
    char symlink_path[512];
    assert(snprintf(symlink_path, sizeof(symlink_path), "%s-link", directory) < (int)sizeof(symlink_path));
    rymga_loader_public_key insecure_key = {"test", {0}};
    rymga_loader_request insecure = {
            .endpoint = "http://example.invalid", .product_id = "demo", .channel = "stable",
            .loader_version = "test", .installation_id = "test", .runtime_directory = directory,
            .trusted_keys = &insecure_key, .trusted_key_count = 1, .timeout_seconds = 1};
    rymga_loader_artifact *insecure_artifact = NULL;
    insecure.endpoint = NULL;
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    insecure.endpoint = "http://example.invalid";
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    insecure.endpoint = "https://example.invalid";
    assert(symlink(directory, symlink_path) == 0);
    insecure.runtime_directory = symlink_path;
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    assert(unlink(symlink_path) == 0);
    insecure.runtime_directory = directory;
    insecure.product_id = "../demo";
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    insecure.product_id = "demo";
    insecure.loader_version = "\xc0\x80";
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    insecure.loader_version = "test";
    insecure.timeout_seconds = 0;
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    insecure.timeout_seconds = 1;
    insecure_key.key_id = NULL;
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    insecure_key.key_id = "test";
    assert(rymga_loader_acquire(&insecure, NULL, 0, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    rymga_loader_public_key duplicate_keys[2] = {insecure_key, insecure_key};
    insecure.trusted_keys = duplicate_keys; insecure.trusted_key_count = 2;
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    insecure.trusted_keys = &insecure_key; insecure.trusted_key_count = 1;
    assert(chmod(directory, 0755) == 0);
    assert(rymga_loader_acquire(&insecure, (const uint8_t *)"license", 7, &insecure_artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    assert(rmdir(directory) == 0);
    char stale_root[] = "/tmp/rymga-loader-stale-test-XXXXXX", stale[512], guarded[512], unexpected[512];
    assert(mkdtemp(stale_root) != NULL);
    assert(rymga_platform_make_temp_directory(stale_root, stale, sizeof(stale)));
    mark_stale(stale); rymga_platform_cleanup_stale(stale_root, 1); assert(access(stale, F_OK) != 0);
    assert(rymga_platform_make_temp_directory(stale_root, guarded, sizeof(guarded)));
    assert(snprintf(unexpected, sizeof(unexpected), "%s/keep.txt", guarded) < (int)sizeof(unexpected));
    FILE *keep = fopen(unexpected, "wb"); assert(keep != NULL); assert(fclose(keep) == 0);
    mark_stale(guarded); rymga_platform_cleanup_stale(stale_root, 1); assert(access(guarded, F_OK) == 0);
    assert(unlink(unexpected) == 0); rymga_platform_remove_directory(guarded); assert(rmdir(stale_root) == 0);
    char plugin_root[] = "/tmp/rymga-loader-plugin-test-XXXXXX", plugin_directory[512], source[512];
    assert(mkdtemp(plugin_root) != NULL);
    assert(snprintf(plugin_directory, sizeof(plugin_directory), "%s/plugins", plugin_root) < (int)sizeof(plugin_directory));
    assert(snprintf(source, sizeof(source), "%s/example.jar", plugin_root) < (int)sizeof(source));
    assert(rymga_platform_ensure_private_directory(plugin_directory));
    FILE *source_file = fopen(source, "wb"); assert(source_file != NULL); assert(fputs("original", source_file) >= 0); assert(fclose(source_file) == 0);
    rymga_platform_plugin plugin = {0}; assert(rymga_platform_materialize_plugin(source, plugin_directory, &plugin));
    assert(unlink(plugin.destination) == 0);
    FILE *replacement = fopen(plugin.destination, "wb"); assert(replacement != NULL); assert(fputs("replacement", replacement) >= 0); assert(fclose(replacement) == 0);
    rymga_platform_release_plugin(&plugin); assert(access(source, F_OK) == 0);
    char destination[512]; assert(snprintf(destination, sizeof(destination), "%s/example.jar", plugin_directory) < (int)sizeof(destination));
    assert(access(destination, F_OK) == 0); assert(unlink(destination) == 0);
    pid_t plugin_child = fork(); assert(plugin_child >= 0);
    if (plugin_child == 0) { rymga_platform_plugin abandoned = {0}; _exit(rymga_platform_materialize_plugin(source, plugin_directory, &abandoned) ? 0 : 1); }
    int plugin_status = 0; assert(waitpid(plugin_child, &plugin_status, 0) == plugin_child && WIFEXITED(plugin_status) && WEXITSTATUS(plugin_status) == 0);
    rymga_platform_cleanup_plugins(plugin_directory); assert(access(destination, F_OK) != 0);
    assert(unlink(source) == 0); assert(rmdir(plugin_directory) == 0); assert(rmdir(plugin_root) == 0);
    const char *endpoint = getenv("RYMGA_INTEGRATION_ENDPOINT");
    if (endpoint == NULL) return 0;

    const uint8_t public_key[] = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
        0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3,
        0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
    rymga_loader_public_key key = {"dev-2026", {0}};
    memcpy(key.public_key, public_key, sizeof(public_key));
    mkdir("/tmp/rymga-loader-runtime", 0700);
    rymga_loader_request request = {
        .endpoint = endpoint, .product_id = "demo-plugin", .channel = "stable", .loader_version = "0.1.0",
        .installation_id = "integration-test", .runtime_directory = "/tmp/rymga-loader-runtime",
        .ca_bundle_path = "/tmp/rymga-loader-tls/localhost.crt", .trusted_keys = &key, .trusted_key_count = 1, .timeout_seconds = 10};
    rymga_loader_artifact *artifact = NULL;
    rymga_loader_result integration_result = rymga_loader_acquire(&request, (const uint8_t *)"dev-license", 11, &artifact);
    if (integration_result != RYMGA_LOADER_OK) fprintf(stderr, "integration acquire: %s\n", rymga_loader_result_string(integration_result));
    assert(integration_result == RYMGA_LOADER_OK);
    FILE *file = fopen(rymga_loader_artifact_path(artifact), "rb");
    assert(file != NULL); unsigned char header[4] = {0}; size_t n = fread(header, 1, sizeof(header), file); fclose(file);
    assert(n == sizeof(header));
    assert(memcmp(header, "PK\003\004", sizeof(header)) == 0);
    rymga_loader_artifact_release(artifact);
    return 0;
}
