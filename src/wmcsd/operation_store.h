// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_OPERATION_STORE_H
#define WMCS_OPERATION_STORE_H

#include "control_wire.h"

enum wmcs_operation_phase {
	WMCS_OPERATION_PENDING = 1,
	WMCS_OPERATION_COMPLETE = 2,
};

struct wmcs_operation_record {
	enum wmcs_operation_phase phase;
	struct wmcs_control_packet request;
	struct wmcs_control_packet response;
};

int wmcs_operation_store_save_pending(
	const char *state_dir,
	const struct wmcs_control_packet *request);
int wmcs_operation_store_save_complete(
	const char *state_dir,
	const struct wmcs_control_packet *request,
	const struct wmcs_control_packet *response);
int wmcs_operation_store_load(
	const char *state_dir,
	struct wmcs_operation_record *operation);
int wmcs_operation_store_remove(const char *state_dir);

#endif
