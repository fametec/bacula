#ifndef __OPENSSL_COPMAT__H__
#define __OPENSSL_COPMAT__H__

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
static inline int EVP_PKEY_up_ref(EVP_PKEY *pkey)
{
	CRYPTO_add(&pkey->references, 1, CRYPTO_LOCK_EVP_PKEY);
	return 1;
}

static inline void EVP_CIPHER_CTX_reset(EVP_CIPHER_CTX *ctx)
{
	EVP_CIPHER_CTX_init(ctx);
}

static inline void EVP_MD_CTX_reset(EVP_MD_CTX *ctx)
{
	EVP_MD_CTX_init(ctx);
}

static inline EVP_MD_CTX *EVP_MD_CTX_new(void)
{
	EVP_MD_CTX *ctx;

	ctx = (EVP_MD_CTX *)OPENSSL_malloc(sizeof(EVP_MD_CTX));
	if (ctx)
		memset(ctx, 0, sizeof(EVP_MD_CTX));
	return ctx;
}

static inline void EVP_MD_CTX_free(EVP_MD_CTX *ctx)
{
	EVP_MD_CTX_reset(ctx);
	OPENSSL_free(ctx);
}

static inline const unsigned char *ASN1_STRING_get0_data(const ASN1_STRING *asn1)
{
	return asn1->data;
}
#endif

#endif
