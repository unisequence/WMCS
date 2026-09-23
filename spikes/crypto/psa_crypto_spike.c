// SPDX-License-Identifier: Apache-2.0

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <psa/crypto.h>

#define PUBLIC_KEY_SIZE 65U
#define SHARED_SECRET_SIZE 32U
#define SIGNATURE_SIZE 64U
#define AEAD_TAG_SIZE 16U

static bool generate_key(psa_key_id_t *key, psa_key_usage_t usage,
			 psa_algorithm_t algorithm)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;

	psa_set_key_usage_flags(&attributes, usage);
	psa_set_key_algorithm(&attributes, algorithm);
	psa_set_key_type(&attributes,
			 PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attributes, 256);
	status = psa_generate_key(&attributes, key);
	psa_reset_key_attributes(&attributes);
	return status == PSA_SUCCESS;
}

int main(void)
{
	static const uint8_t message[] = "wmcs-psa-crypto-spike-v0";
	static const uint8_t info[] = "wmcs-spike-hkdf-v0";
	static const uint8_t nonce[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	static const uint8_t aad[] = "wmcs-spike-aad-v0";
	psa_key_attributes_t public_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_attributes_t aead_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t signer = 0;
	psa_key_id_t verifier = 0;
	psa_key_id_t first_ecdh = 0;
	psa_key_id_t second_ecdh = 0;
	psa_key_id_t aead_key = 0;
	uint8_t first_public[PUBLIC_KEY_SIZE];
	uint8_t second_public[PUBLIC_KEY_SIZE];
	uint8_t first_secret[SHARED_SECRET_SIZE];
	uint8_t second_secret[SHARED_SECRET_SIZE];
	uint8_t derived[SHARED_SECRET_SIZE];
	uint8_t hash[32];
	uint8_t signature[SIGNATURE_SIZE];
	uint8_t ciphertext[sizeof(message) - 1U + AEAD_TAG_SIZE];
	uint8_t plaintext[sizeof(message) - 1U];
	size_t first_public_size = 0;
	size_t second_public_size = 0;
	size_t first_secret_size = 0;
	size_t second_secret_size = 0;
	size_t hash_size = 0;
	size_t signature_size = 0;
	size_t ciphertext_size = 0;
	size_t plaintext_size = 0;
	psa_status_t status;
	int result = 1;

	if (psa_crypto_init() != PSA_SUCCESS)
		goto out;
	if (!generate_key(&signer, PSA_KEY_USAGE_SIGN_HASH,
			  PSA_ALG_ECDSA(PSA_ALG_SHA_256)))
		goto out;
	status = psa_hash_compute(PSA_ALG_SHA_256, message, sizeof(message) - 1U,
				  hash, sizeof(hash), &hash_size);
	if (status != PSA_SUCCESS || hash_size != sizeof(hash))
		goto out;
	status = psa_sign_hash(signer, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash,
			       hash_size, signature, sizeof(signature),
			       &signature_size);
	if (status != PSA_SUCCESS || signature_size != sizeof(signature))
		goto out;
	status = psa_export_public_key(signer, first_public, sizeof(first_public),
				       &first_public_size);
	if (status != PSA_SUCCESS || first_public_size != sizeof(first_public))
		goto out;

	psa_set_key_usage_flags(&public_attributes, PSA_KEY_USAGE_VERIFY_HASH);
	psa_set_key_algorithm(&public_attributes,
			      PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&public_attributes,
			 PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&public_attributes, 256);
	status = psa_import_key(&public_attributes, first_public, first_public_size,
				&verifier);
	psa_reset_key_attributes(&public_attributes);
	if (status != PSA_SUCCESS)
		goto out;
	if (psa_verify_hash(verifier, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash,
			    hash_size, signature, signature_size) != PSA_SUCCESS)
		goto out;

	if (!generate_key(&first_ecdh, PSA_KEY_USAGE_DERIVE, PSA_ALG_ECDH) ||
	    !generate_key(&second_ecdh, PSA_KEY_USAGE_DERIVE, PSA_ALG_ECDH))
		goto out;
	status = psa_export_public_key(first_ecdh, first_public,
				       sizeof(first_public), &first_public_size);
	if (status != PSA_SUCCESS || first_public_size != sizeof(first_public))
		goto out;
	status = psa_export_public_key(second_ecdh, second_public,
				       sizeof(second_public), &second_public_size);
	if (status != PSA_SUCCESS || second_public_size != sizeof(second_public))
		goto out;
	status = psa_raw_key_agreement(PSA_ALG_ECDH, first_ecdh, second_public,
				       second_public_size, first_secret,
				       sizeof(first_secret), &first_secret_size);
	if (status != PSA_SUCCESS || first_secret_size != sizeof(first_secret))
		goto out;
	status = psa_raw_key_agreement(PSA_ALG_ECDH, second_ecdh, first_public,
				       first_public_size, second_secret,
				       sizeof(second_secret), &second_secret_size);
	if (status != PSA_SUCCESS || second_secret_size != sizeof(second_secret) ||
	    memcmp(first_secret, second_secret, sizeof(first_secret)))
		goto out;

	if (mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), hash,
			 sizeof(hash), first_secret, sizeof(first_secret), info,
			 sizeof(info) - 1U, derived, sizeof(derived)))
		goto out;

	psa_set_key_usage_flags(&aead_attributes,
				PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&aead_attributes, PSA_ALG_GCM);
	psa_set_key_type(&aead_attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&aead_attributes, 256);
	status = psa_import_key(&aead_attributes, derived, sizeof(derived),
				&aead_key);
	psa_reset_key_attributes(&aead_attributes);
	if (status != PSA_SUCCESS)
		goto out;
	status = psa_aead_encrypt(aead_key, PSA_ALG_GCM, nonce, sizeof(nonce), aad,
				  sizeof(aad) - 1U, message, sizeof(message) - 1U,
				  ciphertext, sizeof(ciphertext), &ciphertext_size);
	if (status != PSA_SUCCESS || ciphertext_size != sizeof(ciphertext))
		goto out;
	status = psa_aead_decrypt(aead_key, PSA_ALG_GCM, nonce, sizeof(nonce), aad,
				  sizeof(aad) - 1U, ciphertext, ciphertext_size,
				  plaintext, sizeof(plaintext), &plaintext_size);
	if (status != PSA_SUCCESS || plaintext_size != sizeof(plaintext) ||
	    memcmp(plaintext, message, sizeof(plaintext)))
		goto out;

	printf("PSA crypto spike: ok (P-256 ECDSA, ECDH, HKDF-SHA256, AES-256-GCM)\n");
	result = 0;

out:
	memset(first_secret, 0, sizeof(first_secret));
	memset(second_secret, 0, sizeof(second_secret));
	memset(derived, 0, sizeof(derived));
	memset(plaintext, 0, sizeof(plaintext));
	if (aead_key)
		psa_destroy_key(aead_key);
	if (second_ecdh)
		psa_destroy_key(second_ecdh);
	if (first_ecdh)
		psa_destroy_key(first_ecdh);
	if (verifier)
		psa_destroy_key(verifier);
	if (signer)
		psa_destroy_key(signer);
	psa_reset_key_attributes(&public_attributes);
	psa_reset_key_attributes(&aead_attributes);
	return result;
}
