// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>

#include "atomic_file.h"
#include "identity.h"

#define IDENTITY_RECORD_SIZE 40U
#define PEER_RECORD_SIZE 128U
#define GENERATION_RECORD_SIZE 16U
#define FORGET_RECORD_SIZE 56U

static const uint8_t identity_magic[] = {'W', 'M', 'I', 'K'};
static const uint8_t peer_magic[] = {'W', 'M', 'P', 'R'};
static const uint8_t generation_magic[] = {'W', 'M', 'G', 'N'};
static const uint8_t forget_magic[] = {'W', 'M', 'F', 'G'};
static const uint8_t relationship_domain[] = "WMCS-RELATIONSHIP-V0";

void wmcs_secure_zero(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
}

static void put_u64(uint8_t *output, uint64_t value)
{
	size_t i;

	for (i = 0; i < sizeof(value); i++)
		output[i] = (uint8_t)(value >> (56U - i * 8U));
}

static uint64_t get_u64(const uint8_t *input)
{
	uint64_t value = 0;
	size_t i;

	for (i = 0; i < sizeof(value); i++)
		value = (value << 8U) | input[i];
	return value;
}

static bool all_zero(const uint8_t *input, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (input[i])
			return false;
	}
	return true;
}

static int path_join(char *output, size_t output_size, const char *directory,
		     const char *name)
{
	int length = snprintf(output, output_size, "%s/%s", directory, name);

	if (length < 0 || (size_t)length >= output_size)
		return -ENAMETOOLONG;
	return 0;
}

static int check_private_directory(const char *path, bool create)
{
	struct stat state;

	if (lstat(path, &state)) {
		if (errno != ENOENT || !create)
			return errno == ENOENT ? -ENOENT : -errno;
		if (mkdir(path, 0700))
			return -errno;
		if (lstat(path, &state))
			return -errno;
	}

	if (!S_ISDIR(state.st_mode) || S_ISLNK(state.st_mode) ||
	    state.st_uid != geteuid() || (state.st_mode & 0077U))
		return -EPERM;
	return 0;
}

static int read_private_file(const char *path, uint8_t *output, size_t size)
{
	struct stat state;
	size_t offset = 0;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -errno;
	if (fstat(fd, &state) || !S_ISREG(state.st_mode) ||
	    state.st_uid != geteuid() || (state.st_mode & 0077U) ||
	    state.st_size != (off_t)size) {
		close(fd);
		return -EPERM;
	}

	while (offset < size) {
		ssize_t amount = read(fd, output + offset, size - offset);

		if (amount > 0) {
			offset += (size_t)amount;
			continue;
		}
		if (amount < 0 && errno == EINTR)
			continue;
		close(fd);
		return amount < 0 ? -errno : -EIO;
	}
	if (close(fd))
		return -errno;
	return 0;
}

static int import_private_key(struct wmcs_identity *identity,
			      const uint8_t private_key[WMCS_IDENTITY_PRIVATE_SIZE])
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;
	size_t public_size = 0;

	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
	psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&attributes,
			 PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attributes, 256);
	status = psa_import_key(&attributes, private_key,
				WMCS_IDENTITY_PRIVATE_SIZE, &identity->key);
	psa_reset_key_attributes(&attributes);
	if (status != PSA_SUCCESS)
		return -EKEYREJECTED;
	status = psa_export_public_key(identity->key, identity->public_key,
				       sizeof(identity->public_key), &public_size);
	if (status != PSA_SUCCESS || public_size != sizeof(identity->public_key)) {
		psa_destroy_key(identity->key);
		identity->key = 0;
		return -EKEYREJECTED;
	}
	identity->loaded = true;
	return 0;
}

static int load_identity(struct wmcs_identity *identity)
{
	uint8_t record[IDENTITY_RECORD_SIZE];
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	int result;

	result = path_join(path, sizeof(path), identity->state_dir, "identity.key");
	if (result)
		return result;
	result = read_private_file(path, record, sizeof(record));
	if (result)
		return result;
	if (memcmp(record, identity_magic, sizeof(identity_magic)) ||
	    record[4] != 1 || record[5] != 1 || record[6] != 0 ||
	    record[7] != IDENTITY_RECORD_SIZE) {
		wmcs_secure_zero(record, sizeof(record));
		return -EKEYREJECTED;
	}
	result = import_private_key(identity, &record[8]);
	wmcs_secure_zero(record, sizeof(record));
	return result;
}

static int load_generation(struct wmcs_identity *identity)
{
	uint8_t record[GENERATION_RECORD_SIZE];
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	uint64_t generation;
	int result;

	result = path_join(path, sizeof(path), identity->state_dir,
			   "generation.state");
	if (result)
		return result;
	result = read_private_file(path, record, sizeof(record));
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	if (memcmp(record, generation_magic, sizeof(generation_magic)) ||
	    record[4] != 1 || record[5] || record[6] ||
	    record[7] != GENERATION_RECORD_SIZE) {
		wmcs_secure_zero(record, sizeof(record));
		return -EKEYREJECTED;
	}
	generation = get_u64(&record[8]);
	wmcs_secure_zero(record, sizeof(record));
	if (!generation)
		return -EKEYREJECTED;
	identity->generation = generation;
	return 0;
}

static int store_generation(struct wmcs_identity *identity, uint64_t generation)
{
	uint8_t record[GENERATION_RECORD_SIZE] = {0};
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	int result;

	if (!identity || !generation)
		return -EINVAL;
	result = path_join(path, sizeof(path), identity->state_dir,
			   "generation.state");
	if (result)
		return result;
	memcpy(record, generation_magic, sizeof(generation_magic));
	record[4] = 1;
	record[7] = GENERATION_RECORD_SIZE;
	put_u64(&record[8], generation);
	result = wmcs_atomic_file_write(identity->state_dir, path, record,
					 sizeof(record), 0600);
	wmcs_secure_zero(record, sizeof(record));
	return result;
}

int wmcs_identity_hash(const uint8_t *input, size_t input_size,
		       uint8_t output[WMCS_IDENTITY_HASH_SIZE])
{
	size_t output_size = 0;
	psa_status_t status;

	if ((!input && input_size) || !output)
		return -EINVAL;
	status = psa_hash_compute(PSA_ALG_SHA_256, input, input_size, output,
				  WMCS_IDENTITY_HASH_SIZE, &output_size);
	if (status != PSA_SUCCESS || output_size != WMCS_IDENTITY_HASH_SIZE)
		return -EIO;
	return 0;
}

int wmcs_identity_fingerprint(
	const uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE],
	char output[WMCS_IDENTITY_PEER_ID_SIZE + 1U])
{
	uint8_t digest[WMCS_IDENTITY_HASH_SIZE];
	size_t i;
	int result;

	if (!public_key || !output)
		return -EINVAL;
	result = wmcs_identity_hash(public_key, WMCS_IDENTITY_PUBLIC_SIZE, digest);
	if (result)
		return result;
	for (i = 0; i < WMCS_IDENTITY_PEER_ID_SIZE / 2U; i++)
		snprintf(&output[i * 2U], 3, "%02x", digest[i]);
	wmcs_secure_zero(digest, sizeof(digest));
	return 0;
}

static bool peer_id_valid(const char *peer_id)
{
	size_t i;

	if (!peer_id || strlen(peer_id) != WMCS_IDENTITY_PEER_ID_SIZE)
		return false;
	for (i = 0; i < WMCS_IDENTITY_PEER_ID_SIZE; i++) {
		if (!((peer_id[i] >= '0' && peer_id[i] <= '9') ||
		      (peer_id[i] >= 'a' && peer_id[i] <= 'f')))
			return false;
	}
	return true;
}

static int decode_peer_record(const uint8_t input[PEER_RECORD_SIZE],
			      struct wmcs_peer_record *record)
{
	if (memcmp(input, peer_magic, sizeof(peer_magic)) || input[4] != 1 ||
	    (input[5] != WMCS_PEER_ROLE_CONTROLLER &&
	     input[5] != WMCS_PEER_ROLE_AGENT) ||
	    input[6] > WMCS_PEER_STATE_RELEASED ||
	    input[7] != PEER_RECORD_SIZE || !all_zero(&input[113], 15U))
		return -EKEYREJECTED;
	memset(record, 0, sizeof(*record));
	record->role = (enum wmcs_peer_role)input[5];
	record->state = (enum wmcs_peer_state)input[6];
	memcpy(record->public_key, &input[8], sizeof(record->public_key));
	memcpy(record->relationship_key, &input[73],
	       sizeof(record->relationship_key));
	record->generation = get_u64(&input[105]);
	if (record->public_key[0] != 4 || !record->generation)
		return -EKEYREJECTED;
	return 0;
}

int wmcs_identity_load_peer(struct wmcs_identity *identity, const char *peer_id,
			    struct wmcs_peer_record *record)
{
	uint8_t serialized[PEER_RECORD_SIZE];
	char directory[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	char filename[WMCS_IDENTITY_PEER_ID_SIZE + 6U];
	char expected[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 96U];
	int result;

	if (!identity || !peer_id_valid(peer_id) || !record)
		return -EINVAL;
	result = path_join(directory, sizeof(directory), identity->state_dir, "peers");
	if (result)
		return result;
	if (snprintf(filename, sizeof(filename), "%s.peer", peer_id) !=
	    (int)(WMCS_IDENTITY_PEER_ID_SIZE + 5U))
		return -ENAMETOOLONG;
	result = path_join(path, sizeof(path), directory, filename);
	if (result)
		return result;
	result = read_private_file(path, serialized, sizeof(serialized));
	if (result)
		return result;
	result = decode_peer_record(serialized, record);
	wmcs_secure_zero(serialized, sizeof(serialized));
	if (result)
		return result;
	result = wmcs_identity_fingerprint(record->public_key, expected);
	if (result || strcmp(expected, peer_id)) {
		wmcs_secure_zero(record, sizeof(*record));
		return result ? result : -EKEYREJECTED;
	}
	return 0;
}

static int peer_info_compare(const void *left, const void *right)
{
	const struct wmcs_peer_info *first = left;
	const struct wmcs_peer_info *second = right;

	return strcmp(first->peer_id, second->peer_id);
}

int wmcs_identity_list_peers(struct wmcs_identity *identity,
			     struct wmcs_peer_info *peers, size_t capacity,
			     size_t *count)
{
	char directory[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	struct dirent *entry;
	DIR *stream;
	int result;

	if (!identity || !peers || !capacity ||
	    capacity > WMCS_IDENTITY_MAX_PEERS || !count)
		return -EINVAL;
	*count = 0;
	memset(peers, 0, capacity * sizeof(*peers));
	if (!wmcs_identity_ready(identity))
		return 0;
	result = path_join(directory, sizeof(directory), identity->state_dir, "peers");
	if (result)
		return result;
	result = check_private_directory(directory, false);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	stream = opendir(directory);
	if (!stream)
		return -errno;
	while ((entry = readdir(stream)) != NULL) {
		struct wmcs_peer_record record = {0};
		char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
		size_t name_size = strlen(entry->d_name);

		if (name_size != WMCS_IDENTITY_PEER_ID_SIZE + 5U ||
		    strcmp(&entry->d_name[WMCS_IDENTITY_PEER_ID_SIZE], ".peer"))
			continue;
		memcpy(peer_id, entry->d_name, WMCS_IDENTITY_PEER_ID_SIZE);
		peer_id[WMCS_IDENTITY_PEER_ID_SIZE] = '\0';
		if (!peer_id_valid(peer_id))
			continue;
		if (*count >= capacity) {
			result = -EOVERFLOW;
			goto out;
		}
		strcpy(peers[*count].peer_id, peer_id);
		result = wmcs_identity_load_peer(identity, peer_id,
						 &record);
		if (result) {
			wmcs_secure_zero(&record, sizeof(record));
			goto out;
		}
		peers[*count].role = record.role;
		peers[*count].state = record.state;
		peers[*count].generation = record.generation;
		(*count)++;
		wmcs_secure_zero(&record, sizeof(record));
	}
	result = 0;

out:
	if (closedir(stream) && !result)
		result = -errno;
	if (result) {
		wmcs_secure_zero(peers, capacity * sizeof(*peers));
		*count = 0;
		return result;
	}
	qsort(peers, *count, sizeof(*peers), peer_info_compare);
	return 0;
}

static int scan_peers(struct wmcs_identity *identity)
{
	char directory[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	struct dirent *entry;
	DIR *stream;
	int result;

	result = path_join(directory, sizeof(directory), identity->state_dir, "peers");
	if (result)
		return result;
	result = check_private_directory(directory, false);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	stream = opendir(directory);
	if (!stream)
		return -errno;
	while ((entry = readdir(stream)) != NULL) {
		char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
		struct wmcs_peer_record record = {0};
		size_t name_size = strlen(entry->d_name);

		if (name_size != WMCS_IDENTITY_PEER_ID_SIZE + 5U ||
		    strcmp(&entry->d_name[WMCS_IDENTITY_PEER_ID_SIZE], ".peer"))
			continue;
		memcpy(peer_id, entry->d_name, WMCS_IDENTITY_PEER_ID_SIZE);
		peer_id[WMCS_IDENTITY_PEER_ID_SIZE] = '\0';
		if (!peer_id_valid(peer_id))
			continue;
		result = wmcs_identity_load_peer(identity, peer_id, &record);
		if (result) {
			wmcs_secure_zero(&record, sizeof(record));
			closedir(stream);
			return result;
		}
		if (identity->peer_count >= WMCS_IDENTITY_MAX_PEERS) {
			wmcs_secure_zero(&record, sizeof(record));
			closedir(stream);
			return -E2BIG;
		}
		identity->peer_count++;
		if (record.role == WMCS_PEER_ROLE_CONTROLLER &&
		    record.state == WMCS_PEER_STATE_ACTIVE)
			identity->active_controller_count++;
		if (record.generation > identity->generation)
			identity->generation = record.generation;
		wmcs_secure_zero(&record, sizeof(record));
	}
	if (closedir(stream))
		return -errno;
	return 0;
}

int wmcs_identity_init(struct wmcs_identity *identity, const char *state_dir)
{
	int result;

	if (!identity || !state_dir || !state_dir[0] ||
	    strlen(state_dir) >= sizeof(identity->state_dir))
		return -EINVAL;
	memset(identity, 0, sizeof(*identity));
	strcpy(identity->state_dir, state_dir);
	if (psa_crypto_init() != PSA_SUCCESS)
		return -EIO;
	result = check_private_directory(state_dir, false);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	result = load_identity(identity);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	result = load_generation(identity);
	if (result)
		return result;
	return scan_peers(identity);
}

void wmcs_identity_close(struct wmcs_identity *identity)
{
	if (!identity)
		return;
	if (identity->key)
		psa_destroy_key(identity->key);
	wmcs_secure_zero(identity, sizeof(*identity));
}

int wmcs_identity_ensure(struct wmcs_identity *identity)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	uint8_t private_key[WMCS_IDENTITY_PRIVATE_SIZE];
	uint8_t record[IDENTITY_RECORD_SIZE] = {0};
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	size_t private_size = 0;
	size_t public_size = 0;
	psa_status_t status;
	int result;

	if (!identity)
		return -EINVAL;
	if (identity->loaded)
		return 0;
	result = check_private_directory(identity->state_dir, true);
	if (result)
		return result;
	result = path_join(path, sizeof(path), identity->state_dir, "identity.key");
	if (result)
		return result;
	result = load_identity(identity);
	if (!result) {
		result = load_generation(identity);
		if (result)
			return result;
		return scan_peers(identity);
	}
	if (result != -ENOENT)
		return result;

	psa_set_key_usage_flags(&attributes,
				PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
	psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&attributes,
			 PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attributes, 256);
	status = psa_generate_key(&attributes, &identity->key);
	psa_reset_key_attributes(&attributes);
	if (status != PSA_SUCCESS)
		return -EIO;
	status = psa_export_key(identity->key, private_key, sizeof(private_key),
				&private_size);
	if (status != PSA_SUCCESS || private_size != sizeof(private_key)) {
		result = -EIO;
		goto fail;
	}
	status = psa_export_public_key(identity->key, identity->public_key,
				       sizeof(identity->public_key), &public_size);
	if (status != PSA_SUCCESS || public_size != sizeof(identity->public_key)) {
		result = -EIO;
		goto fail;
	}
	memcpy(record, identity_magic, sizeof(identity_magic));
	record[4] = 1;
	record[5] = 1;
	record[7] = IDENTITY_RECORD_SIZE;
	memcpy(&record[8], private_key, sizeof(private_key));
	result = wmcs_atomic_file_write(identity->state_dir, path, record,
					 sizeof(record), 0600);
	if (result)
		goto fail;
	identity->loaded = true;
	wmcs_secure_zero(private_key, sizeof(private_key));
	wmcs_secure_zero(record, sizeof(record));
	return 0;

fail:
	wmcs_secure_zero(private_key, sizeof(private_key));
	wmcs_secure_zero(record, sizeof(record));
	psa_destroy_key(identity->key);
	identity->key = 0;
	wmcs_secure_zero(identity->public_key, sizeof(identity->public_key));
	return result;
}

bool wmcs_identity_ready(const struct wmcs_identity *identity)
{
	return identity && identity->loaded;
}

const uint8_t *wmcs_identity_public_key(const struct wmcs_identity *identity)
{
	return wmcs_identity_ready(identity) ? identity->public_key : NULL;
}

uint64_t wmcs_identity_generation(const struct wmcs_identity *identity)
{
	return identity ? identity->generation : 0;
}

size_t wmcs_identity_peer_count(const struct wmcs_identity *identity)
{
	return identity ? identity->peer_count : 0;
}

size_t wmcs_identity_active_controller_count(
	const struct wmcs_identity *identity)
{
	return identity ? identity->active_controller_count : 0;
}

int wmcs_identity_sign(const struct wmcs_identity *identity,
		       const uint8_t *input, size_t input_size,
		       uint8_t signature[WMCS_IDENTITY_SIGNATURE_SIZE])
{
	uint8_t digest[WMCS_IDENTITY_HASH_SIZE];
	size_t signature_size = 0;
	psa_status_t status;
	int result;

	if (!wmcs_identity_ready(identity) || !signature)
		return -EINVAL;
	result = wmcs_identity_hash(input, input_size, digest);
	if (result)
		return result;
	status = psa_sign_hash(identity->key, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
			       digest, sizeof(digest), signature,
			       WMCS_IDENTITY_SIGNATURE_SIZE, &signature_size);
	wmcs_secure_zero(digest, sizeof(digest));
	if (status != PSA_SUCCESS || signature_size != WMCS_IDENTITY_SIGNATURE_SIZE)
		return -EIO;
	return 0;
}

int wmcs_identity_verify(const uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE],
			 const uint8_t *input, size_t input_size,
			 const uint8_t signature[WMCS_IDENTITY_SIGNATURE_SIZE])
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	uint8_t digest[WMCS_IDENTITY_HASH_SIZE];
	psa_key_id_t key = 0;
	psa_status_t status;
	int result;

	if (!public_key || !signature || public_key[0] != 4)
		return -EINVAL;
	result = wmcs_identity_hash(input, input_size, digest);
	if (result)
		return result;
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
	psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&attributes,
			 PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attributes, 256);
	status = psa_import_key(&attributes, public_key, WMCS_IDENTITY_PUBLIC_SIZE,
				&key);
	psa_reset_key_attributes(&attributes);
	if (status == PSA_SUCCESS)
		status = psa_verify_hash(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
					 digest, sizeof(digest), signature,
					 WMCS_IDENTITY_SIGNATURE_SIZE);
	if (key)
		psa_destroy_key(key);
	wmcs_secure_zero(digest, sizeof(digest));
	return status == PSA_SUCCESS ? 0 : -EKEYREJECTED;
}

int wmcs_identity_ephemeral_create(
	psa_key_id_t *key, uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE])
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	size_t public_size = 0;
	psa_status_t status;

	if (!key || !public_key)
		return -EINVAL;
	*key = 0;
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
	psa_set_key_type(&attributes,
			 PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attributes, 256);
	status = psa_generate_key(&attributes, key);
	psa_reset_key_attributes(&attributes);
	if (status != PSA_SUCCESS)
		return -EIO;
	status = psa_export_public_key(*key, public_key, WMCS_IDENTITY_PUBLIC_SIZE,
				       &public_size);
	if (status != PSA_SUCCESS || public_size != WMCS_IDENTITY_PUBLIC_SIZE) {
		psa_destroy_key(*key);
		*key = 0;
		return -EIO;
	}
	return 0;
}

int wmcs_identity_relationship_key(
	psa_key_id_t local_ephemeral,
	const uint8_t peer_ephemeral[WMCS_IDENTITY_PUBLIC_SIZE],
	const uint8_t nonce[16],
	const uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE],
	uint8_t output[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE])
{
	uint8_t info[sizeof(relationship_domain) - 1U + WMCS_IDENTITY_HASH_SIZE];
	uint8_t secret[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE];
	size_t secret_size = 0;
	psa_status_t status;
	int result = -EIO;

	if (!local_ephemeral || !peer_ephemeral || !nonce || !transcript_hash ||
	    !output)
		return -EINVAL;
	status = psa_raw_key_agreement(PSA_ALG_ECDH, local_ephemeral,
				       peer_ephemeral, WMCS_IDENTITY_PUBLIC_SIZE,
				       secret, sizeof(secret), &secret_size);
	if (status != PSA_SUCCESS || secret_size != sizeof(secret))
		goto out;
	memcpy(info, relationship_domain, sizeof(relationship_domain) - 1U);
	memcpy(&info[sizeof(relationship_domain) - 1U], transcript_hash,
	       WMCS_IDENTITY_HASH_SIZE);
	if (!mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), nonce, 16,
			  secret, sizeof(secret), info, sizeof(info), output,
			  WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE))
		result = 0;

out:
	wmcs_secure_zero(secret, sizeof(secret));
	wmcs_secure_zero(info, sizeof(info));
	return result;
}

int wmcs_identity_store_peer(
	struct wmcs_identity *identity,
	const uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE],
	const uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE],
	enum wmcs_peer_role role,
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U])
{
	uint8_t record[PEER_RECORD_SIZE] = {0};
	char directory[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	char filename[WMCS_IDENTITY_PEER_ID_SIZE + 6U];
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 96U];
	struct wmcs_peer_record existing_peer = {0};
	uint64_t local_generation;
	bool existed;
	int result;

	if (!wmcs_identity_ready(identity) || !public_key || !relationship_key ||
	    !peer_id || (role != WMCS_PEER_ROLE_CONTROLLER &&
			 role != WMCS_PEER_ROLE_AGENT))
		return -EINVAL;
	if (identity->generation == UINT64_MAX)
		return -EOVERFLOW;
	result = wmcs_identity_fingerprint(public_key, peer_id);
	if (result)
		return result;
	result = path_join(directory, sizeof(directory), identity->state_dir, "peers");
	if (result)
		return result;
	result = check_private_directory(directory, true);
	if (result)
		return result;
	if (snprintf(filename, sizeof(filename), "%s.peer", peer_id) !=
	    (int)(WMCS_IDENTITY_PEER_ID_SIZE + 5U))
		return -ENAMETOOLONG;
	result = path_join(path, sizeof(path), directory, filename);
	if (result)
		return result;
	existed = access(path, F_OK) == 0;
	if (!existed && errno != ENOENT)
		return -errno;
	if (existed) {
		result = wmcs_identity_load_peer(identity, peer_id, &existing_peer);
		if (result)
			return result;
		wmcs_secure_zero(&existing_peer, sizeof(existing_peer));
		return -EEXIST;
	}
	if (role == WMCS_PEER_ROLE_CONTROLLER &&
	    identity->active_controller_count) {
		wmcs_secure_zero(&existing_peer, sizeof(existing_peer));
		return -EBUSY;
	}
	if (identity->peer_count >= WMCS_IDENTITY_MAX_PEERS) {
		wmcs_secure_zero(&existing_peer, sizeof(existing_peer));
		return -ENOSPC;
	}
	local_generation = identity->generation + 1U;
	result = store_generation(identity, local_generation);
	if (result) {
		wmcs_secure_zero(&existing_peer, sizeof(existing_peer));
		return result;
	}
	identity->generation = local_generation;
	memcpy(record, peer_magic, sizeof(peer_magic));
	record[4] = 1;
	record[5] = (uint8_t)role;
	record[6] = WMCS_PEER_STATE_ACTIVE;
	record[7] = PEER_RECORD_SIZE;
	memcpy(&record[8], public_key, WMCS_IDENTITY_PUBLIC_SIZE);
	memcpy(&record[73], relationship_key,
	       WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE);
	put_u64(&record[105], 1U);
	result = wmcs_atomic_file_write(directory, path, record, sizeof(record),
					 0600);
	wmcs_secure_zero(record, sizeof(record));
	wmcs_secure_zero(&existing_peer, sizeof(existing_peer));
	if (result)
		return result;
	identity->peer_count++;
	if (role == WMCS_PEER_ROLE_CONTROLLER)
		identity->active_controller_count++;
	return 0;
}

static int advance_peer(struct wmcs_identity *identity, const char *peer_id,
			uint64_t expected_generation, uint64_t next_generation,
			enum wmcs_peer_state next_state, bool change_state)
{
	struct wmcs_peer_record peer;
	uint8_t serialized[PEER_RECORD_SIZE] = {0};
	char directory[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	char filename[WMCS_IDENTITY_PEER_ID_SIZE + 6U];
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 96U];
	enum wmcs_peer_state final_state;
	uint64_t local_generation;
	int result;

	if (!identity || !peer_id_valid(peer_id) || !expected_generation ||
	    identity->generation == UINT64_MAX ||
	    expected_generation == UINT64_MAX ||
	    next_generation != expected_generation + 1U ||
	    (change_state && next_state != WMCS_PEER_STATE_ACTIVE &&
	     next_state != WMCS_PEER_STATE_RELEASED))
		return -EINVAL;
	result = wmcs_identity_load_peer(identity, peer_id, &peer);
	if (result)
		return result;
	if (peer.generation != expected_generation) {
		wmcs_secure_zero(&peer, sizeof(peer));
		return -ESTALE;
	}
	final_state = change_state ? next_state : peer.state;
	if (peer.role == WMCS_PEER_ROLE_CONTROLLER &&
	    peer.state != WMCS_PEER_STATE_ACTIVE &&
	    final_state == WMCS_PEER_STATE_ACTIVE &&
	    identity->active_controller_count) {
		wmcs_secure_zero(&peer, sizeof(peer));
		return -EBUSY;
	}
	result = path_join(directory, sizeof(directory), identity->state_dir, "peers");
	if (result)
		goto out;
	if (snprintf(filename, sizeof(filename), "%s.peer", peer_id) !=
	    (int)(WMCS_IDENTITY_PEER_ID_SIZE + 5U)) {
		result = -ENAMETOOLONG;
		goto out;
	}
	result = path_join(path, sizeof(path), directory, filename);
	if (result)
		goto out;
	local_generation = identity->generation + 1U;
	result = store_generation(identity, local_generation);
	if (result)
		goto out;
	identity->generation = local_generation;
	memcpy(serialized, peer_magic, sizeof(peer_magic));
	serialized[4] = 1;
	serialized[5] = (uint8_t)peer.role;
	serialized[6] = (uint8_t)final_state;
	serialized[7] = PEER_RECORD_SIZE;
	memcpy(&serialized[8], peer.public_key, sizeof(peer.public_key));
	memcpy(&serialized[73], peer.relationship_key,
	       sizeof(peer.relationship_key));
	put_u64(&serialized[105], next_generation);
	result = wmcs_atomic_file_write(directory, path, serialized,
					 sizeof(serialized), 0600);
	if (!result) {
		if (peer.role == WMCS_PEER_ROLE_CONTROLLER &&
		    peer.state != final_state) {
			if (peer.state == WMCS_PEER_STATE_ACTIVE &&
			    identity->active_controller_count)
				identity->active_controller_count--;
			else if (final_state == WMCS_PEER_STATE_ACTIVE)
				identity->active_controller_count++;
		}
	}

out:
	wmcs_secure_zero(serialized, sizeof(serialized));
	wmcs_secure_zero(&peer, sizeof(peer));
	return result;
}

int wmcs_identity_advance_peer_generation(struct wmcs_identity *identity,
					  const char *peer_id,
					  uint64_t expected_generation,
					  uint64_t next_generation)
{
	return advance_peer(identity, peer_id, expected_generation, next_generation,
			    WMCS_PEER_STATE_ACTIVE, false);
}

int wmcs_identity_advance_peer_state(struct wmcs_identity *identity,
				     const char *peer_id,
				     uint64_t expected_generation,
				     uint64_t next_generation,
				     enum wmcs_peer_state next_state)
{
	return advance_peer(identity, peer_id, expected_generation, next_generation,
			    next_state, true);
}

int wmcs_identity_delete_peer(struct wmcs_identity *identity,
			      const char *peer_id,
			      uint64_t expected_generation)
{
	struct wmcs_peer_record peer;
	char directory[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	char filename[WMCS_IDENTITY_PEER_ID_SIZE + 6U];
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 96U];
	uint64_t local_generation;
	int result;

	if (!wmcs_identity_ready(identity) || !peer_id_valid(peer_id) ||
	    !expected_generation || identity->generation == UINT64_MAX)
		return -EINVAL;
	result = wmcs_identity_load_peer(identity, peer_id, &peer);
	if (result)
		return result;
	if (peer.generation != expected_generation) {
		result = -ESTALE;
		goto out;
	}
	result = path_join(directory, sizeof(directory), identity->state_dir, "peers");
	if (result)
		goto out;
	if (snprintf(filename, sizeof(filename), "%s.peer", peer_id) !=
	    (int)(WMCS_IDENTITY_PEER_ID_SIZE + 5U)) {
		result = -ENAMETOOLONG;
		goto out;
	}
	result = path_join(path, sizeof(path), directory, filename);
	if (result)
		goto out;
	local_generation = identity->generation + 1U;
	result = store_generation(identity, local_generation);
	if (result)
		goto out;
	identity->generation = local_generation;
	if (unlink(path)) {
		result = -errno;
		goto out;
	}
	if (identity->peer_count)
		identity->peer_count--;
	if (peer.role == WMCS_PEER_ROLE_CONTROLLER &&
	    peer.state == WMCS_PEER_STATE_ACTIVE &&
	    identity->active_controller_count)
		identity->active_controller_count--;
	result = wmcs_atomic_file_sync_directory(directory);

out:
	wmcs_secure_zero(&peer, sizeof(peer));
	return result;
}

static int forget_path(char output[WMCS_IDENTITY_STATE_DIR_SIZE + 32U],
			       const char *state_dir)
{
	if (!state_dir || !state_dir[0])
		return -EINVAL;
	return path_join(output, WMCS_IDENTITY_STATE_DIR_SIZE + 32U, state_dir,
				 "forget.pending");
}

int wmcs_identity_forget_load(const struct wmcs_identity *identity,
			      struct wmcs_forget_record *record)
{
	uint8_t serialized[FORGET_RECORD_SIZE];
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	int result;

	if (!identity || !record)
		return -EINVAL;
	memset(record, 0, sizeof(*record));
	result = forget_path(path, identity->state_dir);
	if (result)
		return result;
	result = read_private_file(path, serialized, sizeof(serialized));
	if (result)
		return result;
	memcpy(record->peer_id, &serialized[16],
	       WMCS_IDENTITY_PEER_ID_SIZE);
	record->peer_id[WMCS_IDENTITY_PEER_ID_SIZE] = '\0';
	record->generation = get_u64(&serialized[8]);
	if (memcmp(serialized, forget_magic, sizeof(forget_magic)) ||
	    serialized[4] != 1 || serialized[5] != 1 ||
	    ((uint16_t)serialized[6] << 8U | serialized[7]) !=
		    FORGET_RECORD_SIZE ||
	    !record->generation || !peer_id_valid(record->peer_id) ||
	    !all_zero(&serialized[48], 8U)) {
		wmcs_secure_zero(serialized, sizeof(serialized));
		wmcs_secure_zero(record, sizeof(*record));
		return -EKEYREJECTED;
	}
	wmcs_secure_zero(serialized, sizeof(serialized));
	return 0;
}

int wmcs_identity_forget_begin(struct wmcs_identity *identity,
			       const char *peer_id,
			       uint64_t expected_generation)
{
	struct wmcs_forget_record existing = {0};
	struct wmcs_peer_record peer = {0};
	uint8_t serialized[FORGET_RECORD_SIZE] = {0};
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	int result;

	if (!wmcs_identity_ready(identity) || !peer_id_valid(peer_id) ||
	    !expected_generation)
		return -EINVAL;
	result = wmcs_identity_forget_load(identity, &existing);
	if (!result) {
		if (!strcmp(existing.peer_id, peer_id) &&
		    existing.generation == expected_generation)
			result = 0;
		else
			result = -EBUSY;
		goto out;
	}
	if (result != -ENOENT)
		goto out;
	result = wmcs_identity_load_peer(identity, peer_id, &peer);
	if (result)
		goto out;
	if (peer.generation != expected_generation) {
		result = -ESTALE;
		goto out;
	}
	result = forget_path(path, identity->state_dir);
	if (result)
		goto out;
	memcpy(serialized, forget_magic, sizeof(forget_magic));
	serialized[4] = 1;
	serialized[5] = 1;
	serialized[6] = (uint8_t)(FORGET_RECORD_SIZE >> 8U);
	serialized[7] = (uint8_t)FORGET_RECORD_SIZE;
	put_u64(&serialized[8], expected_generation);
	memcpy(&serialized[16], peer_id, WMCS_IDENTITY_PEER_ID_SIZE);
	result = wmcs_atomic_file_write(identity->state_dir, path, serialized,
					 sizeof(serialized), 0600);

out:
	wmcs_secure_zero(&existing, sizeof(existing));
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(serialized, sizeof(serialized));
	return result;
}

int wmcs_identity_forget_finish(const struct wmcs_identity *identity)
{
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	int result;

	if (!identity)
		return -EINVAL;
	result = forget_path(path, identity->state_dir);
	if (result)
		return result;
	if (unlink(path) && errno != ENOENT)
		return -errno;
	return wmcs_atomic_file_sync_directory(identity->state_dir);
}
