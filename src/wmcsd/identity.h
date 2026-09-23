// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_IDENTITY_H
#define WMCS_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <psa/crypto.h>

#define WMCS_IDENTITY_PUBLIC_SIZE 65U
#define WMCS_IDENTITY_PRIVATE_SIZE 32U
#define WMCS_IDENTITY_SIGNATURE_SIZE 64U
#define WMCS_IDENTITY_HASH_SIZE 32U
#define WMCS_IDENTITY_PEER_ID_SIZE 32U
#define WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE 32U
#define WMCS_IDENTITY_STATE_DIR_SIZE 256U
#define WMCS_IDENTITY_MAX_PEERS 16U

enum wmcs_peer_role {
	WMCS_PEER_ROLE_CONTROLLER = 1,
	WMCS_PEER_ROLE_AGENT = 2,
};

enum wmcs_peer_state {
	WMCS_PEER_STATE_ACTIVE = 0,
	WMCS_PEER_STATE_RELEASED = 1,
};

struct wmcs_peer_record {
	uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE];
	uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE];
	enum wmcs_peer_role role;
	enum wmcs_peer_state state;
	uint64_t generation;
};

struct wmcs_peer_info {
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	enum wmcs_peer_role role;
	enum wmcs_peer_state state;
	uint64_t generation;
};

struct wmcs_forget_record {
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	uint64_t generation;
};

struct wmcs_identity {
	char state_dir[WMCS_IDENTITY_STATE_DIR_SIZE];
	psa_key_id_t key;
	uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE];
	uint64_t generation;
	size_t peer_count;
	size_t active_controller_count;
	bool loaded;
};

int wmcs_identity_init(struct wmcs_identity *identity, const char *state_dir);
void wmcs_identity_close(struct wmcs_identity *identity);
int wmcs_identity_ensure(struct wmcs_identity *identity);
bool wmcs_identity_ready(const struct wmcs_identity *identity);
const uint8_t *wmcs_identity_public_key(const struct wmcs_identity *identity);
uint64_t wmcs_identity_generation(const struct wmcs_identity *identity);
size_t wmcs_identity_peer_count(const struct wmcs_identity *identity);
size_t wmcs_identity_active_controller_count(
	const struct wmcs_identity *identity);

int wmcs_identity_hash(const uint8_t *input, size_t input_size,
		       uint8_t output[WMCS_IDENTITY_HASH_SIZE]);
int wmcs_identity_sign(const struct wmcs_identity *identity,
		       const uint8_t *input, size_t input_size,
		       uint8_t signature[WMCS_IDENTITY_SIGNATURE_SIZE]);
int wmcs_identity_verify(const uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE],
			 const uint8_t *input, size_t input_size,
			 const uint8_t signature[WMCS_IDENTITY_SIGNATURE_SIZE]);
int wmcs_identity_fingerprint(
	const uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE],
	char output[WMCS_IDENTITY_PEER_ID_SIZE + 1U]);

int wmcs_identity_ephemeral_create(
	psa_key_id_t *key, uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE]);
int wmcs_identity_relationship_key(
	psa_key_id_t local_ephemeral,
	const uint8_t peer_ephemeral[WMCS_IDENTITY_PUBLIC_SIZE],
	const uint8_t nonce[16],
	const uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE],
	uint8_t output[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE]);

int wmcs_identity_store_peer(
	struct wmcs_identity *identity,
	const uint8_t public_key[WMCS_IDENTITY_PUBLIC_SIZE],
	const uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE],
	enum wmcs_peer_role role,
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U]);
int wmcs_identity_load_peer(struct wmcs_identity *identity, const char *peer_id,
			    struct wmcs_peer_record *record);
int wmcs_identity_list_peers(struct wmcs_identity *identity,
			     struct wmcs_peer_info *peers, size_t capacity,
			     size_t *count);
int wmcs_identity_advance_peer_generation(struct wmcs_identity *identity,
					  const char *peer_id,
					  uint64_t expected_generation,
					  uint64_t next_generation);
int wmcs_identity_advance_peer_state(struct wmcs_identity *identity,
				     const char *peer_id,
				     uint64_t expected_generation,
				     uint64_t next_generation,
				     enum wmcs_peer_state next_state);
int wmcs_identity_delete_peer(struct wmcs_identity *identity,
				      const char *peer_id,
				      uint64_t expected_generation);
int wmcs_identity_forget_begin(struct wmcs_identity *identity,
				       const char *peer_id,
				       uint64_t expected_generation);
int wmcs_identity_forget_load(const struct wmcs_identity *identity,
				      struct wmcs_forget_record *record);
int wmcs_identity_forget_finish(const struct wmcs_identity *identity);

void wmcs_secure_zero(void *buffer, size_t size);

#endif
