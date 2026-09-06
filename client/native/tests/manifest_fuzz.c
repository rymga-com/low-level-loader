#include <stddef.h>
#include <stdint.h>

int rymga_loader_fuzz_manifest(const uint8_t *bytes, size_t length);

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t length) {
    rymga_loader_fuzz_manifest(bytes, length);
    return 0;
}
