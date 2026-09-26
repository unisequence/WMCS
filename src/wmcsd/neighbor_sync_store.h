// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_NEIGHBOR_SYNC_STORE_H
#define WMCS_NEIGHBOR_SYNC_STORE_H

#include <stddef.h>

#include "identity.h"
#include "neighbor_sync_wire.h"

int wmcs_nr_store_load(const struct wmcs_identity *identity,
		       const struct wmcs_nr_record *own,
		       struct wmcs_nr_record records[WMCS_NR_SET_MAX],
		       size_t *count);
int wmcs_nr_store_save(const struct wmcs_identity *identity,
		       const struct wmcs_nr_record *own,
		       const struct wmcs_nr_record *records,
		       size_t count);

#endif
