#ifndef RYMGA_LOADER_H
#define RYMGA_LOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RYMGA_LOADER_PUBLIC_KEY_BYTES 32

typedef enum {
    RYMGA_LOADER_OK = 0,
    RYMGA_LOADER_INVALID_ARGUMENT,
    RYMGA_LOADER_NETWORK_ERROR,
    RYMGA_LOADER_AUTHORIZATION_DENIED,
    RYMGA_LOADER_PROTOCOL_ERROR,
    RYMGA_LOADER_SIGNATURE_ERROR,
    RYMGA_LOADER_IO_ERROR,
    RYMGA_LOADER_INTEGRITY_ERROR,
    RYMGA_LOADER_OUT_OF_MEMORY
} rymga_loader_result;

typedef struct {
    const char *key_id;
    uint8_t public_key[RYMGA_LOADER_PUBLIC_KEY_BYTES];
} rymga_loader_public_key;

typedef struct {
    const char *endpoint;
    const char *product_id;
    const char *channel;
    const char *loader_version;
    const char *installation_id;
    const char *runtime_directory;
    const char *ca_bundle_path;
    const rymga_loader_public_key *trusted_keys;
    size_t trusted_key_count;
    long timeout_seconds;
} rymga_loader_request;

typedef struct rymga_loader_artifact rymga_loader_artifact;

rymga_loader_result rymga_loader_acquire(const rymga_loader_request *request,
                                         const uint8_t *license, size_t license_length,
                                         rymga_loader_artifact **artifact_out);
const char *rymga_loader_artifact_path(const rymga_loader_artifact *artifact);
void rymga_loader_artifact_release(rymga_loader_artifact *artifact);
const char *rymga_loader_result_string(rymga_loader_result result);

#ifdef __cplusplus
}
#endif

#endif
