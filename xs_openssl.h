/* copyright (c) 2022 - 2026 grunfink et al. / MIT license */

#ifndef _XS_OPENSSL_H

#define _XS_OPENSSL_H

xs_str *_xs_digest(const xs_val *input, int size, const char *digest, int as_hex);

#ifndef _XS_MD5_H
#define xs_md5_hex(input, size)       _xs_digest(input, size, "md5",    1)
#endif /* XS_MD5_H */

#ifndef _XS_BASE64_H
xs_str *xs_base64_enc(const xs_val *data, int sz);
xs_val *xs_base64_dec(const xs_str *data, int *size);
#endif /* XS_BASE64_H */

#define xs_sha1_hex(input, size)      _xs_digest(input, size, "sha1",   1)
#define xs_sha256_hex(input, size)    _xs_digest(input, size, "sha256", 1)
#define xs_sha256_base64(input, size) _xs_digest(input, size, "sha256", 0)

xs_dict *xs_evp_genkey(int bits);
xs_str *xs_evp_sign(const char *secret, const char *mem, int size);
int xs_evp_verify(const char *pubkey, const char *mem, int size, const char *b64sig);


#ifdef XS_IMPLEMENTATION

#ifdef SNAC_ESP32

#include <string.h>

#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#ifndef _XS_BASE64_H

xs_str *xs_base64_enc(const xs_val *data, int sz)
/* encodes data to base64 */
{
    size_t out_len = 0;

    /* get required size */
    if (mbedtls_base64_encode(NULL, 0, &out_len, (const unsigned char *)data, (size_t)sz) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
        return NULL;

    xs_str *s = xs_realloc(NULL, _xs_blk_size((int)out_len + 1));

    if (mbedtls_base64_encode((unsigned char *)s, out_len, &out_len, (const unsigned char *)data, (size_t)sz) != 0) {
        xs_free(s);
        return NULL;
    }

    s[out_len] = '\0';
    return s;
}

xs_val *xs_base64_dec(const xs_str *data, int *size)
/* decodes data from base64 */
{
    size_t in_len = strlen(data);
    size_t out_len = 0;

    /* conservative alloc: base64 expands by 4/3 */
    xs_str *out = xs_realloc(NULL, _xs_blk_size((int)in_len + 1));

    if (mbedtls_base64_decode((unsigned char *)out, in_len, &out_len, (const unsigned char *)data, in_len) != 0) {
        xs_free(out);
        return NULL;
    }

    out = xs_realloc(out, _xs_blk_size((int)out_len + 1));
    out[out_len] = '\0';
    *size = (int)out_len;
    return out;
}

#endif /* _XS_BASE64_H */

xs_str *_xs_digest(const xs_val *input, int size, const char *digest, int as_hex)
/* generic function for generating and encoding digests */
{
    const mbedtls_md_info_t *info = NULL;

    if (strcmp(digest, "md5") == 0)        info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    else if (strcmp(digest, "sha1") == 0)  info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    else if (strcmp(digest, "sha256") == 0)info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    else                                  return NULL;

    unsigned char out[64];
    if (mbedtls_md(info, (const unsigned char *)input, (size_t)size, out) != 0)
        return NULL;

    int out_sz = (int)mbedtls_md_get_size(info);

    return as_hex ? xs_hex_enc((char *)out, out_sz) :
                    xs_base64_enc((char *)out, out_sz);
}

static int _xs_rng(void *ctx, unsigned char *out, size_t out_len)
{
    return mbedtls_ctr_drbg_random((mbedtls_ctr_drbg_context *)ctx, out, out_len);
}

xs_dict *xs_evp_genkey(int bits)
/* generates an RSA keypair (PEM strings) */
{
    xs_dict *keypair = NULL;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr;
    mbedtls_pk_context pk;
    const char *pers = "snac2_keygen";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr);
    mbedtls_pk_init(&pk);

    if (mbedtls_ctr_drbg_seed(&ctr, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0)
        goto end;

    if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0)
        goto end;

    if (mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), _xs_rng, &ctr, (unsigned int)bits, 65537) != 0)
        goto end;

    unsigned char sec[4096];
    unsigned char pub[2048];

    if (mbedtls_pk_write_key_pem(&pk, sec, sizeof(sec)) != 0)
        goto end;

    if (mbedtls_pk_write_pubkey_pem(&pk, pub, sizeof(pub)) != 0)
        goto end;

    keypair = xs_dict_new();
    keypair = xs_dict_append(keypair, "secret", (const char *)sec);
    keypair = xs_dict_append(keypair, "public", (const char *)pub);

end:
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr);
    mbedtls_entropy_free(&entropy);

    return keypair;
}

xs_str *xs_evp_sign(const char *secret, const char *mem, int size)
/* signs a memory block (secret is in PEM format) */
{
    xs_str *signature = NULL;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr;
    mbedtls_pk_context pk;
    const char *pers = "snac2_sign";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr);
    mbedtls_pk_init(&pk);

    if (mbedtls_ctr_drbg_seed(&ctr, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0)
        goto end;

    if (mbedtls_pk_parse_key(&pk, (const unsigned char *)secret, strlen(secret) + 1, NULL, 0, _xs_rng, &ctr) != 0)
        goto end;

    unsigned char hash[32];
    if (mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                   (const unsigned char *)mem, (size_t)size, hash) != 0)
        goto end;

    unsigned char sig[1024];
    size_t sig_len = 0;

    if (mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, 0, sig, sizeof(sig), &sig_len, _xs_rng, &ctr) != 0)
        goto end;

    signature = xs_base64_enc((const char *)sig, (int)sig_len);

end:
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr);
    mbedtls_entropy_free(&entropy);

    return signature;
}

int xs_evp_verify(const char *pubkey, const char *mem, int size, const char *b64sig)
/* verifies a base64 block, returns non-zero on ok */
{
    int r = 0;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    if (mbedtls_pk_parse_public_key(&pk, (const unsigned char *)pubkey, strlen(pubkey) + 1) != 0)
        goto end;

    unsigned char hash[32];
    if (mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                   (const unsigned char *)mem, (size_t)size, hash) != 0)
        goto end;

    int s_size = 0;
    xs *sig = xs_base64_dec(b64sig, &s_size);
    if (sig == NULL)
        goto end;

    r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 0, (const unsigned char *)sig, (size_t)s_size) == 0;

end:
    mbedtls_pk_free(&pk);
    return r;
}

#else /* SNAC_ESP32 */

#include "openssl/rsa.h"
#include "openssl/pem.h"
#include "openssl/evp.h"


#ifndef _XS_BASE64_H

xs_str *xs_base64_enc(const xs_val *data, int sz)
/* encodes data to base64 */
{
    BIO *mem, *b64;
    BUF_MEM *bptr;
 
    b64 = BIO_new(BIO_f_base64());
    mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(b64, data, sz);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    int n = bptr->length;
    xs_str *s = xs_realloc(NULL, _xs_blk_size(n + 1));

    memcpy(s, bptr->data, n);
    s[n] = '\0';

    BIO_free_all(b64);

    return s;
}


xs_val *xs_base64_dec(const xs_str *data, int *size)
/* decodes data from base64 */
{
    BIO *b64, *mem;

    *size = strlen(data);

    b64 = BIO_new(BIO_f_base64());
    mem = BIO_new_mem_buf(data, *size);
    b64 = BIO_push(b64, mem);

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    /* alloc a very big buffer */
    xs_str *s = xs_realloc(NULL, *size);

    *size = BIO_read(b64, s, *size);

    /* adjust to current size */
    s = xs_realloc(s, _xs_blk_size(*size + 1));
    s[*size] = '\0';

    BIO_free_all(b64);

    return s;
}

#endif /* _XS_BASE64_H */


xs_str *_xs_digest(const xs_val *input, int size, const char *digest, int as_hex)
/* generic function for generating and encoding digests */
{
    const EVP_MD *md;

    if ((md = EVP_get_digestbyname(digest)) == NULL)
        return NULL;

    unsigned char output[1024];
    unsigned int out_size;
    EVP_MD_CTX *mdctx;

    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, input, size);
    EVP_DigestFinal_ex(mdctx, output, &out_size);
    EVP_MD_CTX_free(mdctx);

    return as_hex ? xs_hex_enc   ((char *)output, out_size) :
                    xs_base64_enc((char *)output, out_size);
}


xs_dict *xs_evp_genkey(int bits)
/* generates an RSA keypair using the EVP interface */
{
    xs_dict *keypair = NULL;
    EVP_PKEY_CTX *ctx;
    EVP_PKEY *pkey = NULL;

    if ((ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL)) == NULL)
        goto end;

    if (EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0 ||
        EVP_PKEY_keygen(ctx, &pkey) <= 0)
        goto end;

    BIO *bs = BIO_new(BIO_s_mem());
    BIO *bp = BIO_new(BIO_s_mem());
    BUF_MEM *sptr;
    BUF_MEM *pptr;

    PEM_write_bio_PrivateKey(bs, pkey, NULL, NULL, 0, 0, NULL);
    BIO_get_mem_ptr(bs, &sptr);

    PEM_write_bio_PUBKEY(bp, pkey);
    BIO_get_mem_ptr(bp, &pptr);

    keypair = xs_dict_new();

    keypair = xs_dict_append(keypair, "secret", sptr->data);
    keypair = xs_dict_append(keypair, "public", pptr->data);

    BIO_free(bs);
    BIO_free(bp);

end:
    return keypair;
}


xs_str *xs_evp_sign(const char *secret, const char *mem, int size)
/* signs a memory block (secret is in PEM format) */
{
    xs_str *signature = NULL;
    BIO *b;
    unsigned char *sig;
    unsigned int sig_len;
    EVP_PKEY *pkey;
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;

    /* un-PEM the key */
    b = BIO_new_mem_buf(secret, strlen(secret));
    pkey = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);

    /* I've learnt all these magical incantations by watching
       the Python module code and the OpenSSL manual pages */
    /* Well, "learnt" may be an overstatement */

    md = EVP_get_digestbyname("sha256");

    mdctx = EVP_MD_CTX_new();

    sig_len = EVP_PKEY_size(pkey);
    sig = xs_realloc(NULL, sig_len);

    EVP_SignInit(mdctx, md);
    EVP_SignUpdate(mdctx, mem, size);

    if (EVP_SignFinal(mdctx, sig, &sig_len, pkey) == 1)
        signature = xs_base64_enc((char *)sig, sig_len);

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    BIO_free(b);
    xs_free(sig);

    return signature;
}


int xs_evp_verify(const char *pubkey, const char *mem, int size, const char *b64sig)
/* verifies a base64 block, returns non-zero on ok */
{
    int r = 0;
    BIO *b;
    EVP_PKEY *pkey;
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;

    /* un-PEM the key */
    b = BIO_new_mem_buf(pubkey, strlen(pubkey));
    pkey = PEM_read_bio_PUBKEY(b, NULL, NULL, NULL);

    md = EVP_get_digestbyname("sha256");
    mdctx = EVP_MD_CTX_new();

    if (pkey != NULL) {
        xs *sig = NULL;
        int s_size;

        /* de-base64 */
        sig = xs_base64_dec(b64sig,  &s_size);

        if (sig != NULL) {
            EVP_VerifyInit(mdctx, md);
            EVP_VerifyUpdate(mdctx, mem, size);

            r = EVP_VerifyFinal(mdctx, (unsigned char *)sig, s_size, pkey);
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    BIO_free(b);

    return r;
}

#endif /* SNAC_ESP32 */


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_OPENSSL_H */
