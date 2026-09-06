#include "rymga_loader.h"
#include "rymga_embedded_config.h"

#include <jni.h>
#include <sodium.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void throw_io(JNIEnv *env, rymga_loader_result result) {
    jclass type = (*env)->FindClass(env, "java/io/IOException");
    if (type != NULL) (*env)->ThrowNew(env, type, rymga_loader_result_string(result));
}

static uint8_t *chars_utf8(const jchar *input, jsize length, size_t *size_out) {
    if (length < 0 || (size_t)length > (SIZE_MAX - 1) / 3) return NULL;
    uint8_t *output = calloc((size_t)length * 3 + 1, 1); size_t size = 0;
    if (output != NULL) for (jsize i = 0; i < length; i++) {
        uint32_t codepoint = input[i];
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && i + 1 < length && input[i + 1] >= 0xdc00 && input[i + 1] <= 0xdfff) {
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (input[++i] - 0xdc00);
        } else if (codepoint >= 0xd800 && codepoint <= 0xdfff) { sodium_memzero(output, (size_t)length * 3 + 1); free(output); output = NULL; break; }
        if (codepoint <= 0x7f) output[size++] = (uint8_t)codepoint;
        else if (codepoint <= 0x7ff) { output[size++] = 0xc0 | (codepoint >> 6); output[size++] = 0x80 | (codepoint & 0x3f); }
        else if (codepoint <= 0xffff) { output[size++] = 0xe0 | (codepoint >> 12); output[size++] = 0x80 | ((codepoint >> 6) & 0x3f); output[size++] = 0x80 | (codepoint & 0x3f); }
        else { output[size++] = 0xf0 | (codepoint >> 18); output[size++] = 0x80 | ((codepoint >> 12) & 0x3f); output[size++] = 0x80 | ((codepoint >> 6) & 0x3f); output[size++] = 0x80 | (codepoint & 0x3f); }
    }
    if (output != NULL) *size_out = size;
    return output;
}

static char *string_utf8(JNIEnv *env, jstring value) {
    if (value == NULL) return NULL;
    jsize length = (*env)->GetStringLength(env, value);
    const jchar *input = (*env)->GetStringChars(env, value, NULL);
    if (input == NULL) return NULL;
    size_t size = 0; uint8_t *output = chars_utf8(input, length, &size);
    (*env)->ReleaseStringChars(env, value, input);
    return (char *)output;
}

static uint8_t *license_utf8(JNIEnv *env, jcharArray chars, size_t *size_out) {
    jsize length = (*env)->GetArrayLength(env, chars);
    if (length <= 0 || length > 4096) return NULL;
    jchar *input = (*env)->GetCharArrayElements(env, chars, NULL);
    if (input == NULL) return NULL;
    uint8_t *output = chars_utf8(input, length, size_out);
    sodium_memzero(input, (size_t)length * sizeof(*input));
    (*env)->ReleaseCharArrayElements(env, chars, input, 0);
    if (output == NULL || *size_out == 0 || *size_out > 4096) {
        if (output != NULL) { sodium_memzero(output, (size_t)length * 3 + 1); free(output); }
        return NULL;
    }
    return output;
}

static jstring new_utf8(JNIEnv *env, const char *input) {
    if (input == NULL) return NULL;
    size_t bytes = strlen(input), offset = 0, length = 0;
    jchar *output = malloc((bytes + 1) * sizeof(*output));
    if (output == NULL) return NULL;
    while (offset < bytes) {
        uint32_t codepoint; size_t extra;
        uint8_t first = (uint8_t)input[offset++];
        if (first < 0x80) { codepoint = first; extra = 0; }
        else if ((first & 0xe0) == 0xc0) { codepoint = first & 0x1f; extra = 1; }
        else if ((first & 0xf0) == 0xe0) { codepoint = first & 0x0f; extra = 2; }
        else if ((first & 0xf8) == 0xf0) { codepoint = first & 0x07; extra = 3; }
        else { free(output); return NULL; }
        if (extra > bytes - offset) { free(output); return NULL; }
        for (size_t index = 0; index < extra; index++) {
            uint8_t next = (uint8_t)input[offset++];
            if ((next & 0xc0) != 0x80) { free(output); return NULL; }
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((extra == 1 && codepoint < 0x80) || (extra == 2 && codepoint < 0x800) ||
            (extra == 3 && codepoint < 0x10000) || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            free(output); return NULL;
        }
        if (codepoint <= 0xffff) output[length++] = (jchar)codepoint;
        else { codepoint -= 0x10000; output[length++] = (jchar)(0xd800 | (codepoint >> 10)); output[length++] = (jchar)(0xdc00 | (codepoint & 0x3ff)); }
    }
    jstring result = (*env)->NewString(env, output, (jsize)length);
    free(output);
    return result;
}

static bool hex_key(const char *input, uint8_t output[RYMGA_LOADER_PUBLIC_KEY_BYTES]) {
    if (input == NULL || strlen(input) != RYMGA_LOADER_PUBLIC_KEY_BYTES * 2) return false;
    for (size_t index = 0; index < RYMGA_LOADER_PUBLIC_KEY_BYTES; index++) {
        int high = input[index * 2], low = input[index * 2 + 1];
        high = high >= '0' && high <= '9' ? high - '0' : high >= 'a' && high <= 'f' ? high - 'a' + 10 : high >= 'A' && high <= 'F' ? high - 'A' + 10 : -1;
        low = low >= '0' && low <= '9' ? low - '0' : low >= 'a' && low <= 'f' ? low - 'a' + 10 : low >= 'A' && low <= 'F' ? low - 'A' + 10 : -1;
        if (high < 0 || low < 0) return false;
        output[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

JNIEXPORT jlong JNICALL Java_com_rymga_loader_NativeLoader_acquire0(JNIEnv *env, jclass type, jstring endpoint,
        jstring product, jstring channel, jstring version, jstring installation, jstring runtime, jstring ca_bundle,
        jobjectArray key_ids, jobjectArray public_keys, jcharArray license) {
    (void)type;
    if (endpoint == NULL || product == NULL || channel == NULL || version == NULL || installation == NULL || runtime == NULL ||
        key_ids == NULL || public_keys == NULL || license == NULL) { throw_io(env, RYMGA_LOADER_INVALID_ARGUMENT); return 0; }
    jsize key_count = (*env)->GetArrayLength(env, key_ids);
    if (key_count < 1 || key_count > 16 || (*env)->GetArrayLength(env, public_keys) != key_count) {
        throw_io(env, RYMGA_LOADER_INVALID_ARGUMENT); return 0;
    }
    char *endpoint_bytes = string_utf8(env, endpoint), *product_bytes = string_utf8(env, product), *channel_bytes = string_utf8(env, channel),
        *version_bytes = string_utf8(env, version), *installation_bytes = string_utf8(env, installation),
        *runtime_bytes = string_utf8(env, runtime), *ca_bytes = string_utf8(env, ca_bundle);
    rymga_loader_public_key *keys = calloc((size_t)key_count, sizeof(*keys)); bool valid = keys != NULL;
    for (jsize index = 0; valid && index < key_count; index++) {
        jstring key_id = (*env)->GetObjectArrayElement(env, key_ids, index);
        jbyteArray public_key = (*env)->GetObjectArrayElement(env, public_keys, index);
        valid = key_id != NULL && public_key != NULL && (*env)->GetArrayLength(env, public_key) == 32 &&
            (keys[index].key_id = string_utf8(env, key_id)) != NULL;
        if (valid) (*env)->GetByteArrayRegion(env, public_key, 0, 32, (jbyte *)keys[index].public_key);
        if (key_id != NULL) (*env)->DeleteLocalRef(env, key_id);
        if (public_key != NULL) (*env)->DeleteLocalRef(env, public_key);
        if ((*env)->ExceptionCheck(env)) valid = false;
    }
    if (endpoint_bytes == NULL || product_bytes == NULL || channel_bytes == NULL || version_bytes == NULL || installation_bytes == NULL ||
        runtime_bytes == NULL || (ca_bundle != NULL && ca_bytes == NULL)) valid = false;
    size_t license_size = 0; uint8_t *license_bytes = (*env)->ExceptionCheck(env) ? NULL : license_utf8(env, license, &license_size);
    rymga_loader_artifact *artifact = NULL;
    rymga_loader_request request = { .endpoint = endpoint_bytes, .product_id = product_bytes, .channel = channel_bytes, .loader_version = version_bytes,
        .installation_id = installation_bytes, .runtime_directory = runtime_bytes, .ca_bundle_path = ca_bytes,
        .trusted_keys = keys, .trusted_key_count = (size_t)key_count, .timeout_seconds = 30 };
    rymga_loader_result result = !valid || license_bytes == NULL ? RYMGA_LOADER_INVALID_ARGUMENT : rymga_loader_acquire(&request, license_bytes, license_size, &artifact);
    if (license_bytes != NULL) { sodium_memzero(license_bytes, license_size); free(license_bytes); }
    if (keys != NULL) {
        for (jsize index = 0; index < key_count; index++) free((void *)keys[index].key_id);
        sodium_memzero(keys, (size_t)key_count * sizeof(*keys)); free(keys);
    }
    free(ca_bytes); free(runtime_bytes); free(installation_bytes); free(version_bytes); free(channel_bytes); free(product_bytes); free(endpoint_bytes);
    if (result != RYMGA_LOADER_OK) { if (!(*env)->ExceptionCheck(env)) throw_io(env,result); return 0; }
    return (jlong)(uintptr_t)artifact;
}

JNIEXPORT jlong JNICALL Java_com_rymga_loader_NativeLoader_acquireEmbedded0(JNIEnv *env, jclass type,
        jstring runtime, jstring ca_bundle, jcharArray license) {
    (void)type;
    if (!RYMGA_EMBEDDED_CONFIG || runtime == NULL || license == NULL) {
        throw_io(env, RYMGA_LOADER_INVALID_ARGUMENT); return 0;
    }
    char *runtime_bytes = string_utf8(env, runtime), *ca_bytes = string_utf8(env, ca_bundle);
    size_t license_size = 0;
    uint8_t *license_bytes = (*env)->ExceptionCheck(env) ? NULL : license_utf8(env, license, &license_size);
    rymga_loader_public_key key = { .key_id = RYMGA_EMBEDDED_KEY_ID };
    bool valid = runtime_bytes != NULL && (ca_bundle == NULL || ca_bytes != NULL) && license_bytes != NULL &&
        hex_key(RYMGA_EMBEDDED_PUBLIC_KEY_HEX, key.public_key);
    rymga_loader_request request = { .endpoint = RYMGA_EMBEDDED_ENDPOINT, .product_id = RYMGA_EMBEDDED_PRODUCT,
        .channel = RYMGA_EMBEDDED_CHANNEL, .loader_version = RYMGA_EMBEDDED_LOADER_VERSION,
        .installation_id = RYMGA_EMBEDDED_INSTALLATION_ID, .runtime_directory = runtime_bytes,
        .ca_bundle_path = ca_bytes, .trusted_keys = &key, .trusted_key_count = 1, .timeout_seconds = 30 };
    rymga_loader_artifact *artifact = NULL;
    rymga_loader_result result = valid ? rymga_loader_acquire(&request, license_bytes, license_size, &artifact)
                                       : RYMGA_LOADER_INVALID_ARGUMENT;
    if (license_bytes != NULL) { sodium_memzero(license_bytes, license_size); free(license_bytes); }
    sodium_memzero(key.public_key, sizeof(key.public_key));
    free(ca_bytes); free(runtime_bytes);
    if (result != RYMGA_LOADER_OK) { if (!(*env)->ExceptionCheck(env)) throw_io(env, result); return 0; }
    return (jlong)(uintptr_t)artifact;
}

JNIEXPORT jstring JNICALL Java_com_rymga_loader_NativeLoader_path0(JNIEnv *env, jclass type, jlong handle) {
    (void)type; const char *path = rymga_loader_artifact_path((rymga_loader_artifact *)(uintptr_t)handle);
    return new_utf8(env, path);
}

JNIEXPORT void JNICALL Java_com_rymga_loader_NativeLoader_release0(JNIEnv *env, jclass type, jlong handle) {
    (void)env; (void)type; rymga_loader_artifact_release((rymga_loader_artifact *)(uintptr_t)handle);
}
