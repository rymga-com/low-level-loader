#include "rymga_loader.h"
#include "rymga_platform.h"
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(void) {
    assert(strcmp(rymga_loader_result_string(RYMGA_LOADER_SIGNATURE_ERROR), "signature error") == 0);
    assert(rymga_loader_acquire(NULL, NULL, 0, NULL) == RYMGA_LOADER_INVALID_ARGUMENT);
    rymga_loader_public_key key = {"test", {0}};
    rymga_loader_request request = {
            .endpoint = "http://example.invalid", .product_id = "demo", .channel = "stable",
            .loader_version = "test", .installation_id = "test", .runtime_directory = "C:/missing",
            .trusted_keys = &key, .trusted_key_count = 1, .timeout_seconds = 1};
    rymga_loader_artifact *artifact = NULL;
    assert(rymga_loader_acquire(&request, (const uint8_t *)"license", 7, &artifact) == RYMGA_LOADER_INVALID_ARGUMENT);
    wchar_t temporary[MAX_PATH], candidate[MAX_PATH]; char candidate_utf8[MAX_PATH * 3];
    assert(GetTempPathW(MAX_PATH, temporary) > 0);
    assert(GetTempFileNameW(temporary, L"rym", 0, candidate) != 0);
    assert(DeleteFileW(candidate));
    assert(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, candidate, -1, candidate_utf8, sizeof(candidate_utf8), NULL, NULL) > 0);
    assert(rymga_platform_ensure_private_directory(candidate_utf8));
    assert(rymga_platform_runtime_directory_ok(candidate_utf8));
    char stale[RYMGA_PLATFORM_PATH_MAX], marker[RYMGA_PLATFORM_PATH_MAX];
    assert(rymga_platform_make_temp_directory(candidate_utf8, stale, sizeof(stale)));
    const char *stale_name = strrchr(stale, '/'); stale_name = stale_name == NULL ? stale : stale_name + 1;
    assert(snprintf(marker, sizeof(marker), "%s/.rymga-owner", stale) < (int)sizeof(marker));
    FILE *marker_file = fopen(marker, "wb"); assert(marker_file != NULL);
    assert(fprintf(marker_file, "RLL1 4294967295 1 %s\n", stale_name) > 0); assert(fclose(marker_file) == 0);
    rymga_platform_cleanup_stale(candidate_utf8, 1);
    assert(GetFileAttributesA(stale) == INVALID_FILE_ATTRIBUTES);
    char plugin_directory[RYMGA_PLATFORM_PATH_MAX], source[RYMGA_PLATFORM_PATH_MAX], destination[RYMGA_PLATFORM_PATH_MAX];
    assert(snprintf(plugin_directory, sizeof(plugin_directory), "%s/plugins", candidate_utf8) < (int)sizeof(plugin_directory));
    assert(snprintf(source, sizeof(source), "%s/example.jar", candidate_utf8) < (int)sizeof(source));
    assert(snprintf(destination, sizeof(destination), "%s/example.jar", plugin_directory) < (int)sizeof(destination));
    assert(rymga_platform_ensure_private_directory(plugin_directory));
    FILE *source_file = fopen(source, "wb"); assert(source_file != NULL); assert(fputs("original", source_file) >= 0); assert(fclose(source_file) == 0);
    rymga_platform_plugin plugin = {0}; assert(rymga_platform_materialize_plugin(source, plugin_directory, &plugin));
    assert(DeleteFileA(plugin.destination));
    FILE *replacement = fopen(plugin.destination, "wb"); assert(replacement != NULL); assert(fputs("replacement", replacement) >= 0); assert(fclose(replacement) == 0);
    rymga_platform_release_plugin(&plugin); assert(GetFileAttributesA(destination) != INVALID_FILE_ATTRIBUTES); assert(DeleteFileA(destination));
    rymga_platform_plugin abandoned = {0}; assert(rymga_platform_materialize_plugin(source, plugin_directory, &abandoned));
    FILE *owner = fopen(abandoned.marker, "rb"); assert(owner != NULL); char owner_content[800] = {0};
    assert(fread(owner_content, 1, sizeof(owner_content) - 1, owner) > 0); assert(fclose(owner) == 0);
    char *owner_rest = strchr(owner_content + 5, ' '); assert(owner_rest != NULL);
    owner = fopen(abandoned.marker, "wb"); assert(owner != NULL); assert(fprintf(owner, "RLP1 4294967295%s", owner_rest) > 0); assert(fclose(owner) == 0);
    rymga_platform_cleanup_plugins(plugin_directory); assert(GetFileAttributesA(destination) == INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileA(source)); wchar_t plugin_wide[RYMGA_PLATFORM_PATH_MAX];
    assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, plugin_directory, -1, plugin_wide, RYMGA_PLATFORM_PATH_MAX) > 0);
    assert(RemoveDirectoryW(plugin_wide));
    assert(RemoveDirectoryW(candidate));
    return 0;
}
