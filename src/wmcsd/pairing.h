// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_PAIRING_H
#define WMCS_PAIRING_H

#include <stdbool.h>
#include <stdint.h>

#include <net/if.h>
#include <netinet/in.h>

#include <libubox/uloop.h>

#include "identity.h"
#include "pairing_crypto.h"
#include "pairing_wire.h"

#define WMCS_PAIRING_MIN_SECONDS 5U
#define WMCS_PAIRING_MAX_SECONDS 300U

enum wmcs_pairing_state {
	WMCS_PAIRING_IDLE = 0,
	WMCS_PAIRING_EXCHANGING,
	WMCS_PAIRING_AWAITING_CONFIRMATION,
};

struct wmcs_pairing {
	const char *role;
	char interface[IF_NAMESIZE];
	struct wmcs_identity *identity;
	int fd;
	bool active;
	enum wmcs_pairing_state state;
	struct uloop_fd socket_event;
	struct uloop_timeout deadline;
	struct uloop_timeout retry_timer;
	struct sockaddr_in peer_address;
	struct wmcs_pairing_message request;
	struct wmcs_pairing_message response;
	psa_key_id_t ephemeral_key;
	uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE];
	char sas[WMCS_PAIRING_SAS_SIZE];
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
};

int wmcs_pairing_init(struct wmcs_pairing *pairing, const char *role,
		      const char *interface, struct wmcs_identity *identity);
void wmcs_pairing_close(struct wmcs_pairing *pairing);
int wmcs_pairing_start_agent(struct wmcs_pairing *pairing,
			     uint32_t duration_seconds);
int wmcs_pairing_start_controller(struct wmcs_pairing *pairing,
				  const char *address,
				  uint32_t duration_seconds);
void wmcs_pairing_stop(struct wmcs_pairing *pairing);
int wmcs_pairing_confirm(struct wmcs_pairing *pairing, const char *sas,
			 char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U]);
bool wmcs_pairing_active(const struct wmcs_pairing *pairing);
enum wmcs_pairing_state wmcs_pairing_state(const struct wmcs_pairing *pairing);
const char *wmcs_pairing_state_name(enum wmcs_pairing_state state);
const char *wmcs_pairing_sas_value(const struct wmcs_pairing *pairing);
const char *wmcs_pairing_peer_id(const struct wmcs_pairing *pairing);

#endif
