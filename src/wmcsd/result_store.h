// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_RESULT_STORE_H
#define WMCS_RESULT_STORE_H

#include "control_wire.h"

int wmcs_result_store_save(
	const char *state_dir,
	const struct wmcs_control_packet *response);
int wmcs_result_store_load(
	const char *state_dir,
	struct wmcs_control_packet *response);
int wmcs_result_store_remove(const char *state_dir);

#endif
