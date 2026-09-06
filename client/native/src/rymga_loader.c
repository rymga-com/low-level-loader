#include "rymga_loader.h"
#include "rymga_platform.h"

#include <curl/curl.h>
#include <sodium.h>

#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { REQUEST_MAX = 8192, MANIFEST_MAX = 4096, NONCE_SIZE = 32, SESSION_ID_SIZE = 32, CLOCK_SKEW_SECONDS = 120,
       HASH_SIZE = 32, SIGNATURE_SIZE = 64 };
static const int64_t ARTIFACT_MAX = INT64_C(2147483648);
typedef struct { uint8_t *data; size_t length, capacity, limit; } buffer;
typedef struct { const uint8_t *data; size_t length, offset; } reader;
typedef struct {
    char key_id[65], product[129], channel[65], artifact_id[129], filename[256];
    uint8_t nonce[NONCE_SIZE], hash[HASH_SIZE], signature[SIGNATURE_SIZE];
    int64_t issued_at, expires_at, artifact_size; size_t signed_length;
} manifest;
struct rymga_loader_artifact { char *directory; char *path; };
static atomic_int curl_state;

static void wipe(void *p, size_t n) { if (p != NULL && n != 0) sodium_memzero(p, n); }
static bool curl_ready(void) {
    int expected = 0;
    if (atomic_compare_exchange_strong(&curl_state, &expected, 1)) {
        atomic_store(&curl_state, curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 2 : 3);
    } else {
        while (atomic_load(&curl_state) == 1) { }
    }
    return atomic_load(&curl_state) == 2;
}
static bool starts_with(const char *s, const char *prefix) { return s != NULL && strncmp(s, prefix, strlen(prefix)) == 0; }
static bool identifier(const char *s, size_t max) {
    size_t n = s == NULL ? 0 : strlen(s); if (n == 0 || n > max) return false;
    for (size_t i = 0; i < n; i++) if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
        (s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == '_' || s[i] == '-')) return false;
    return true;
}
static bool text_field(const char *s, size_t max) {
    const uint8_t *p = (const uint8_t *)s; size_t n = s == NULL ? 0 : strlen(s);
    if (n == 0 || n > max) return false;
    for (size_t i = 0; i < n;) { uint32_t cp; size_t extra;
        if (p[i] < 0x80) { cp=p[i++]; extra=0; }
        else if ((p[i]&0xe0)==0xc0) { cp=p[i++]&0x1f; extra=1; }
        else if ((p[i]&0xf0)==0xe0) { cp=p[i++]&0x0f; extra=2; }
        else if ((p[i]&0xf8)==0xf0) { cp=p[i++]&0x07; extra=3; } else return false;
        if (extra > n-i) return false;
        for (size_t j = 0; j < extra; j++) {
            if ((p[i] & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (p[i++] & 0x3f);
        }
        if ((extra==1 && cp<0x80) || (extra==2 && cp<0x800) || (extra==3 && cp<0x10000) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff) || cp<0x20 || (cp>=0x7f && cp<=0x9f)) return false;
    } return true;
}
static bool filename_ok(const char *s) {
    size_t n = s == NULL ? 0 : strlen(s), base = 0;
    if (n <= 4 || n > 255) return false;
    while (base < n && s[base] != '.') base++;
    if (base == 0) return false;
    for (size_t index = 0; index < n; index++) if (strchr("<>:\"/\\|?*", s[index]) != NULL) return false;
    char upper[5] = {0};
    if (base <= 4) for (size_t index = 0; index < base; index++)
        upper[index] = s[index] >= 'a' && s[index] <= 'z' ? (char)(s[index] - 'a' + 'A') : s[index];
    bool reserved = (base == 3 && (!strcmp(upper, "CON") || !strcmp(upper, "PRN") || !strcmp(upper, "AUX") || !strcmp(upper, "NUL"))) ||
        (base == 4 && (!memcmp(upper, "COM", 3) || !memcmp(upper, "LPT", 3)) && upper[3] >= '1' && upper[3] <= '9');
    return !reserved && s[n - 1] != '.' && s[n - 1] != ' ' &&
        s[n-4] == '.' && (s[n-3] == 'j' || s[n-3] == 'J') && (s[n-2] == 'a' || s[n-2] == 'A') && (s[n-1] == 'r' || s[n-1] == 'R');
}
static bool b_reserve(buffer *b, size_t add) {
    if (add > b->limit - b->length) return false;
    if (b->length + add <= b->capacity) return true;
    size_t next = b->capacity == 0 ? 256 : b->capacity;
    while (next < b->length + add) { if (next > b->limit / 2) { next = b->limit; break; } next *= 2; }
    uint8_t *p = realloc(b->data, next); if (p == NULL) return false; b->data = p; b->capacity = next; return true;
}
static bool b_put(buffer *b, const void *p, size_t n) { if (!b_reserve(b, n)) return false; memcpy(b->data + b->length, p, n); b->length += n; return true; }
static bool b_u16(buffer *b, size_t x) { uint8_t v[2] = { (uint8_t)(x >> 8), (uint8_t)x }; return x <= UINT16_MAX && b_put(b, v, 2); }
static bool b_i64(buffer *b, int64_t x) { uint8_t v[8]; for (int i = 7; i >= 0; --i) v[7-i] = (uint8_t)((uint64_t)x >> (i * 8)); return b_put(b, v, 8); }
static bool b_string(buffer *b, const uint8_t *s, size_t n) { return n != 0 && b_u16(b, n) && b_put(b, s, n); }
static void b_free(buffer *b) { wipe(b->data, b->capacity); free(b->data); memset(b, 0, sizeof(*b)); }
static bool r_take(reader *r, void *out, size_t n) { if (n > r->length - r->offset) return false; memcpy(out, r->data + r->offset, n); r->offset += n; return true; }
static bool r_u16(reader *r, size_t *out) { uint8_t v[2]; if (!r_take(r, v, 2)) return false; *out = ((size_t)v[0] << 8) | v[1]; return true; }
static bool r_i64(reader *r, int64_t *out) { uint8_t v[8]; uint64_t x = 0; if (!r_take(r,v,8)) return false; for (size_t i=0;i<8;i++) x=(x<<8)|v[i]; *out=(int64_t)x; return true; }
static bool r_string(reader *r, char *out, size_t cap) {
    size_t n;
    if (!r_u16(r,&n) || n == 0 || n >= cap || n > r->length-r->offset || !r_take(r,out,n) || memchr(out, '\0', n) != NULL) return false;
    out[n]='\0'; return text_field(out, cap - 1);
}
static bool parse_manifest(const uint8_t *bytes, size_t length, manifest *m) {
    reader r = { bytes, length, 0 }; uint8_t magic[4], version[2], session_id[SESSION_ID_SIZE];
    if (length > MANIFEST_MAX || !r_take(&r,magic,4) || memcmp(magic,"RLSM",4) || !r_take(&r,version,2) || version[0] || version[1] != 1 ||
        !r_string(&r,m->key_id,sizeof(m->key_id)) || !identifier(m->key_id,64) || !r_take(&r,m->nonce,NONCE_SIZE) || !r_take(&r,session_id,SESSION_ID_SIZE) ||
        !r_i64(&r,&m->issued_at) || !r_i64(&r,&m->expires_at) || m->issued_at < 0 || m->expires_at <= m->issued_at ||
        !r_string(&r,m->product,sizeof(m->product)) || !identifier(m->product,128) || !r_string(&r,m->channel,sizeof(m->channel)) || !identifier(m->channel,64) ||
        !r_string(&r,m->artifact_id,sizeof(m->artifact_id)) || !identifier(m->artifact_id,128) || !r_string(&r,m->filename,sizeof(m->filename)) || !filename_ok(m->filename) ||
        !r_i64(&r,&m->artifact_size) || m->artifact_size < 0 || m->artifact_size > ARTIFACT_MAX || !r_take(&r,m->hash,HASH_SIZE)) return false;
    m->signed_length = r.offset;
    return r_take(&r,m->signature,SIGNATURE_SIZE) && r.offset == r.length;
}

#ifdef RYMGA_ENABLE_FUZZER
int rymga_loader_fuzz_manifest(const uint8_t *bytes, size_t length) {
    manifest value;
    bool parsed = parse_manifest(bytes, length, &value);
    wipe(&value, sizeof(value));
    return parsed ? 1 : 0;
}
#endif

static const rymga_loader_public_key *find_key(const rymga_loader_request *q, const char *key_id) {
    for (size_t i = 0; i < q->trusted_key_count; i++) if (strcmp(q->trusted_keys[i].key_id, key_id) == 0) return &q->trusted_keys[i];
    return NULL;
}
static size_t receive_memory(char *p, size_t size, size_t count, void *opaque) {
    buffer *b = opaque; if (count != 0 && size > SIZE_MAX / count) return 0; size_t n = size * count;
    return b_put(b, p, n) ? n : 0;
}
static rymga_loader_result perform(CURL *curl, const char *url, const char *method, struct curl_slist *headers,
                                   const uint8_t *body, size_t body_length, buffer *response, const char *ca_bundle, long timeout) {
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_memory); curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout); curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L); curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L); curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2); curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    if (ca_bundle != NULL) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);
    if (strcmp(method, "POST") == 0) { curl_easy_setopt(curl, CURLOPT_POST, 1L); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body); curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_length); }
    CURLcode code = curl_easy_perform(curl); long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (code != CURLE_OK) return RYMGA_LOADER_NETWORK_ERROR;
    if (status == 401 || status == 403) return RYMGA_LOADER_AUTHORIZATION_DENIED;
    return status >= 200 && status < 300 ? RYMGA_LOADER_OK : RYMGA_LOADER_NETWORK_ERROR;
}
static bool encode_request(const rymga_loader_request *q, const uint8_t nonce[NONCE_SIZE], const uint8_t *license, size_t license_length, buffer *out) {
    int64_t now = (int64_t)time(NULL); const uint8_t magic[4] = {'R','L','S','R'};
    return now >= 0 && b_put(out,magic,4) && b_u16(out,1) && b_i64(out,now) && b_put(out,nonce,NONCE_SIZE) &&
        b_string(out,(const uint8_t *)q->product_id,strlen(q->product_id)) && b_string(out,(const uint8_t *)q->channel,strlen(q->channel)) &&
        b_string(out,(const uint8_t *)q->loader_version,strlen(q->loader_version)) && b_string(out,(const uint8_t *)q->installation_id,strlen(q->installation_id)) &&
        b_string(out,license,license_length);
}
typedef struct { rymga_platform_file file; int64_t expected, written; crypto_hash_sha256_state hash; bool failed; } download;
static size_t receive_file(char *p, size_t size, size_t count, void *opaque) {
    download *d = opaque; if (count != 0 && size > SIZE_MAX / count) return 0; size_t n = size * count;
    if (n > (size_t)(d->expected - d->written)) { d->failed = true; return 0; }
    if (!rymga_platform_write(d->file, p, n)) { d->failed = true; return 0; }
    crypto_hash_sha256_update(&d->hash, (const unsigned char *)p, (unsigned long long)n); d->written += (int64_t)n; return n;
}
static bool b64url(const uint8_t *input, size_t input_length, char **out) {
    size_t cap = sodium_base64_ENCODED_LEN(input_length, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    char *encoded = malloc(cap); if (encoded == NULL) return false;
    sodium_bin2base64(encoded, cap, input, input_length, sodium_base64_VARIANT_URLSAFE_NO_PADDING); *out = encoded; return true;
}
static void sensitive_headers_free(struct curl_slist *headers) {
    for (struct curl_slist *item = headers; item != NULL; item = item->next)
        if (item->data != NULL) wipe(item->data, strlen(item->data));
    curl_slist_free_all(headers);
}
static rymga_loader_result download_artifact(CURL *curl, const rymga_loader_request *q, const manifest *m,
                                             const uint8_t *encoded_manifest, size_t encoded_length,
                                             rymga_loader_artifact **out) {
    char template_path[RYMGA_PLATFORM_PATH_MAX], partial[RYMGA_PLATFORM_PATH_MAX], final[RYMGA_PLATFORM_PATH_MAX], url[RYMGA_PLATFORM_PATH_MAX], *token = NULL; bool final_created = false;
    if (!rymga_platform_runtime_directory_ok(q->runtime_directory) || !rymga_platform_make_temp_directory(q->runtime_directory, template_path, sizeof(template_path))) return RYMGA_LOADER_IO_ERROR;
    if (snprintf(partial,sizeof(partial),"%s/.artifact.part",template_path) >= (int)sizeof(partial) ||
        snprintf(final,sizeof(final),"%s/%s",template_path,m->filename) >= (int)sizeof(final) ||
        snprintf(url,sizeof(url),"%s/v1/artifact",q->endpoint) >= (int)sizeof(url)) {
        rymga_platform_remove_directory(template_path);
        return RYMGA_LOADER_IO_ERROR;
    }
    rymga_platform_file file;
#ifdef _WIN32
    file = INVALID_HANDLE_VALUE;
#else
    file = -1;
#endif
    if (!rymga_platform_create_file(partial, &file)) { rymga_platform_remove_directory(template_path); return RYMGA_LOADER_IO_ERROR; }
    rymga_loader_result result = RYMGA_LOADER_NETWORK_ERROR; struct curl_slist *headers = NULL; download d = { .file=file, .expected=m->artifact_size };
    if (!b64url(encoded_manifest,encoded_length,&token) || crypto_hash_sha256_init(&d.hash) != 0) { result = RYMGA_LOADER_OUT_OF_MEMORY; goto cleanup; }
    size_t hlen = strlen(token) + sizeof("Authorization: RymgaSession "); char *auth = malloc(hlen);
    if (auth == NULL) { result = RYMGA_LOADER_OUT_OF_MEMORY; goto cleanup; }
    snprintf(auth, hlen, "Authorization: RymgaSession %s", token); headers = curl_slist_append(headers,auth); wipe(auth, hlen); free(auth);
    if (headers == NULL) { result = RYMGA_LOADER_OUT_OF_MEMORY; goto cleanup; }
    curl_easy_reset(curl); curl_easy_setopt(curl,CURLOPT_URL,url); curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive_file); curl_easy_setopt(curl,CURLOPT_WRITEDATA,&d);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,q->timeout_seconds); curl_easy_setopt(curl,CURLOPT_TIMEOUT,q->timeout_seconds);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L); curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1024L); curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,10L);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L); curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L); curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L); curl_easy_setopt(curl,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2); curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
    if (q->ca_bundle_path != NULL) curl_easy_setopt(curl, CURLOPT_CAINFO, q->ca_bundle_path);
    CURLcode code=curl_easy_perform(curl); long status=0; curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status); uint8_t digest[HASH_SIZE]; crypto_hash_sha256_final(&d.hash,digest);
    if (code != CURLE_OK || status < 200 || status >= 300 || d.failed || d.written != d.expected || sodium_memcmp(digest,m->hash,HASH_SIZE) != 0) { result = RYMGA_LOADER_INTEGRITY_ERROR; wipe(digest,sizeof(digest)); goto cleanup; }
    wipe(digest,sizeof(digest)); if (!rymga_platform_sync_close(&file)) { result = RYMGA_LOADER_IO_ERROR; goto cleanup; }
    if (!rymga_platform_rename(partial,final)) { result=RYMGA_LOADER_IO_ERROR; goto cleanup; } final_created = true;
    rymga_loader_artifact *artifact=calloc(1,sizeof(*artifact)); if (artifact == NULL) { result=RYMGA_LOADER_OUT_OF_MEMORY; goto cleanup; }
    artifact->directory=strdup(template_path); artifact->path=strdup(final); if (artifact->directory == NULL || artifact->path == NULL) { rymga_loader_artifact_release(artifact); result=RYMGA_LOADER_OUT_OF_MEMORY; goto cleanup; }
    *out=artifact; result=RYMGA_LOADER_OK;
cleanup: rymga_platform_close(&file); sensitive_headers_free(headers); if (token != NULL) wipe(token, strlen(token)); free(token); if(result != RYMGA_LOADER_OK) { rymga_platform_remove_file(partial); if (final_created) rymga_platform_remove_file(final); rymga_platform_remove_directory(template_path); } return result;
}

rymga_loader_result rymga_loader_acquire(const rymga_loader_request *q, const uint8_t *license, size_t license_length,
                                         rymga_loader_artifact **artifact_out) {
    if (artifact_out == NULL) return RYMGA_LOADER_INVALID_ARGUMENT;
    *artifact_out = NULL;
    if (q == NULL || !starts_with(q->endpoint,"https://") || strchr(q->endpoint,'?') != NULL || strchr(q->endpoint,'#') != NULL ||
        !identifier(q->product_id,128) || !identifier(q->channel,64) || !text_field(q->loader_version,64) || !text_field(q->installation_id,128) ||
        !rymga_platform_ensure_private_directory(q->runtime_directory) || q->trusted_keys == NULL || q->trusted_key_count == 0 || q->trusted_key_count > 16 || q->timeout_seconds < 1 || q->timeout_seconds > 120 ||
        license == NULL || license_length == 0 || license_length > 4096 || sodium_init() < 0) return RYMGA_LOADER_INVALID_ARGUMENT;
    for (size_t index = 0; index < q->trusted_key_count; index++) {
        if (!identifier(q->trusted_keys[index].key_id, 64)) return RYMGA_LOADER_INVALID_ARGUMENT;
        for (size_t previous = 0; previous < index; previous++)
            if (strcmp(q->trusted_keys[index].key_id, q->trusted_keys[previous].key_id) == 0) return RYMGA_LOADER_INVALID_ARGUMENT;
    }
    rymga_platform_cleanup_stale(q->runtime_directory, 86400);
    if (!curl_ready()) return RYMGA_LOADER_NETWORK_ERROR;
    buffer request = { .limit = REQUEST_MAX }, response = { .limit = MANIFEST_MAX }; uint8_t nonce[NONCE_SIZE]; rymga_loader_result result = RYMGA_LOADER_PROTOCOL_ERROR;
    CURL *curl = NULL; struct curl_slist *headers = NULL; char url[RYMGA_PLATFORM_PATH_MAX]; manifest m;
    randombytes_buf(nonce, sizeof(nonce));
    if (!encode_request(q,nonce,license,license_length,&request) ||
        snprintf(url,sizeof(url),"%s/v1/session",q->endpoint) >= (int)sizeof(url)) { result=RYMGA_LOADER_OUT_OF_MEMORY; goto done; }
    curl = curl_easy_init(); if (curl == NULL) { result=RYMGA_LOADER_NETWORK_ERROR; goto done; }
    headers=curl_slist_append(headers,"Content-Type: application/vnd.rymga.loader-session-request");
    if (headers == NULL) { result=RYMGA_LOADER_OUT_OF_MEMORY; goto done; }
    result=perform(curl,url,"POST",headers,request.data,request.length,&response,q->ca_bundle_path,q->timeout_seconds); if(result != RYMGA_LOADER_OK) goto done;
    if (!parse_manifest(response.data,response.length,&m) || sodium_memcmp(nonce,m.nonce,NONCE_SIZE) != 0 || strcmp(q->product_id,m.product) || strcmp(q->channel,m.channel)) { result=RYMGA_LOADER_PROTOCOL_ERROR; goto done; }
    int64_t now=(int64_t)time(NULL); const rymga_loader_public_key *key=find_key(q,m.key_id);
    if (key == NULL || (m.issued_at > now && m.issued_at - now > CLOCK_SKEW_SECONDS) ||
        (now > m.expires_at && now - m.expires_at > CLOCK_SKEW_SECONDS) ||
        crypto_sign_verify_detached(m.signature,response.data,m.signed_length,key->public_key) != 0) { result=RYMGA_LOADER_SIGNATURE_ERROR; goto done; }
    result=download_artifact(curl,q,&m,response.data,response.length,artifact_out);
done: curl_slist_free_all(headers); curl_easy_cleanup(curl); b_free(&request); b_free(&response); wipe(nonce,sizeof(nonce)); wipe(&m,sizeof(m)); return result;
}
const char *rymga_loader_artifact_path(const rymga_loader_artifact *artifact) { return artifact == NULL ? NULL : artifact->path; }
void rymga_loader_artifact_release(rymga_loader_artifact *artifact) {
    if (artifact == NULL) return;
    if (artifact->path != NULL) rymga_platform_remove_file(artifact->path);
    if (artifact->directory != NULL) rymga_platform_remove_directory(artifact->directory);
    free(artifact->path); free(artifact->directory); wipe(artifact,sizeof(*artifact)); free(artifact);
}
const char *rymga_loader_result_string(rymga_loader_result result) {
    static const char *const labels[] = {"ok","invalid argument","network error","authorization denied","protocol error","signature error","io error","integrity error","out of memory"};
    return result >= RYMGA_LOADER_OK && result <= RYMGA_LOADER_OUT_OF_MEMORY ? labels[result] : "unknown error";
}
