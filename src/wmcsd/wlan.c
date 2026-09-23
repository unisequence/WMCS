// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libubox/blobmsg.h>
#include <libubus.h>
#include <uci.h>

#include "atomic_file.h"
#include "discovery.h"
#include "wlan.h"

#define WIRELESS_PATH "/etc/config/wireless"
#define BACKUP_HEADER_SIZE 96U
#define WIRELESS_MAX_SIZE (256U * 1024U)

static const uint8_t backup_magic[] = {'W', 'M', 'R', 'B'};

struct backup_record {
	uint64_t sequence;
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	mode_t mode;
	uint8_t digest[WMCS_IDENTITY_HASH_SIZE];
	uint8_t *data;
	size_t data_size;
};

static int join_path(char *output, size_t output_size, const char *directory,
		     const char *name)
{
	int length = snprintf(output, output_size, "%s/%s", directory, name);

	return length < 0 || (size_t)length >= output_size ? -ENAMETOOLONG : 0;
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

static void put_u32(uint8_t *output, uint32_t value)
{
	output[0] = (uint8_t)(value >> 24U);
	output[1] = (uint8_t)(value >> 16U);
	output[2] = (uint8_t)(value >> 8U);
	output[3] = (uint8_t)value;
}

static uint32_t get_u32(const uint8_t *input)
{
	return (uint32_t)input[0] << 24U | (uint32_t)input[1] << 16U |
	       (uint32_t)input[2] << 8U | input[3];
}

static int read_all(int fd, uint8_t *output, size_t size)
{
	size_t offset = 0;

	while (offset < size) {
		ssize_t amount = read(fd, output + offset, size - offset);

		if (amount > 0) {
			offset += (size_t)amount;
			continue;
		}
		if (amount < 0 && errno == EINTR)
			continue;
		return amount < 0 ? -errno : -EIO;
	}
	return 0;
}

static int read_regular_file(const char *path, size_t maximum, bool private,
			     uint8_t **data, size_t *size, mode_t *mode)
{
	struct stat state;
	uint8_t *buffer;
	int result;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -errno;
	if (fstat(fd, &state) || !S_ISREG(state.st_mode) || state.st_size < 0 ||
	    (private &&
	     (state.st_uid != geteuid() || (state.st_mode & 0077U))) ||
	    (uintmax_t)state.st_size > maximum) {
		close(fd);
		return -EPERM;
	}
	buffer = malloc(state.st_size ? (size_t)state.st_size : 1U);
	if (!buffer) {
		close(fd);
		return -ENOMEM;
	}
	result = read_all(fd, buffer, (size_t)state.st_size);
	if (close(fd) && !result)
		result = -errno;
	if (result) {
		free(buffer);
		return result;
	}
	*data = buffer;
	*size = (size_t)state.st_size;
	if (mode)
		*mode = state.st_mode & 0777U;
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

static int backup_path(char output[WMCS_IDENTITY_STATE_DIR_SIZE + 32U],
		       const char *state_dir)
{
	return join_path(output, WMCS_IDENTITY_STATE_DIR_SIZE + 32U, state_dir,
			 "wireless.pending");
}

static int backup_create(const char *state_dir, const char *peer_id,
			 uint64_t sequence)
{
	uint8_t header[BACKUP_HEADER_SIZE] = {0};
	uint8_t *wireless = NULL;
	uint8_t *serialized = NULL;
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	mode_t mode;
	size_t wireless_size;
	size_t total_size;
	int result;

	if (!peer_id_valid(peer_id) || !sequence)
		return -EINVAL;
	result = backup_path(path, state_dir);
	if (result)
		return result;
	if (!access(path, F_OK))
		return -EALREADY;
	if (errno != ENOENT)
		return -errno;
	result = read_regular_file(WIRELESS_PATH, WIRELESS_MAX_SIZE, false, &wireless,
				   &wireless_size, &mode);
	if (result)
		return result;
	if (wireless_size > UINT32_MAX) {
		result = -EFBIG;
		goto out;
	}
	memcpy(header, backup_magic, sizeof(backup_magic));
	header[4] = 0;
	header[5] = (uint8_t)((mode >> 8U) & 0xffU);
	header[6] = (uint8_t)(mode & 0xffU);
	put_u64(&header[8], sequence);
	memcpy(&header[16], peer_id, WMCS_IDENTITY_PEER_ID_SIZE);
	put_u32(&header[48], (uint32_t)wireless_size);
	result = wmcs_identity_hash(wireless, wireless_size, &header[52]);
	if (result)
		goto out;
	total_size = BACKUP_HEADER_SIZE + wireless_size;
	serialized = malloc(total_size);
	if (!serialized) {
		result = -ENOMEM;
		goto out;
	}
	memcpy(serialized, header, sizeof(header));
	memcpy(&serialized[BACKUP_HEADER_SIZE], wireless, wireless_size);
	result = wmcs_atomic_file_write(state_dir, path, serialized, total_size,
					 0600);

out:
	if (serialized) {
		wmcs_secure_zero(serialized, total_size);
		free(serialized);
	}
	if (wireless) {
		wmcs_secure_zero(wireless, wireless_size);
		free(wireless);
	}
	wmcs_secure_zero(header, sizeof(header));
	return result;
}

static void backup_record_clear(struct backup_record *record)
{
	if (record->data) {
		wmcs_secure_zero(record->data, record->data_size);
		free(record->data);
	}
	wmcs_secure_zero(record, sizeof(*record));
}

static int backup_load(const char *state_dir, struct backup_record *record)
{
	uint8_t *serialized = NULL;
	uint8_t digest[WMCS_IDENTITY_HASH_SIZE];
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	mode_t ignored_mode;
	size_t serialized_size;
	size_t data_size;
	int result;

	memset(record, 0, sizeof(*record));
	result = backup_path(path, state_dir);
	if (result)
		return result;
	result = read_regular_file(path, BACKUP_HEADER_SIZE + WIRELESS_MAX_SIZE, true,
				   &serialized, &serialized_size, &ignored_mode);
	if (result)
		return result;
	if (serialized_size < BACKUP_HEADER_SIZE ||
	    memcmp(serialized, backup_magic, sizeof(backup_magic)) ||
	    serialized[4] != 0 || serialized[7] ||
	    memcmp(&serialized[84], (uint8_t[12]){0}, 12)) {
		result = -EKEYREJECTED;
		goto out;
	}
	data_size = get_u32(&serialized[48]);
	if (data_size != serialized_size - BACKUP_HEADER_SIZE) {
		result = -EKEYREJECTED;
		goto out;
	}
	memcpy(record->peer_id, &serialized[16], WMCS_IDENTITY_PEER_ID_SIZE);
	record->peer_id[WMCS_IDENTITY_PEER_ID_SIZE] = '\0';
	if (!peer_id_valid(record->peer_id)) {
		result = -EKEYREJECTED;
		goto out;
	}
	record->sequence = get_u64(&serialized[8]);
	record->mode = (mode_t)serialized[5] << 8U | serialized[6];
	if (!record->sequence || !record->mode || (record->mode & ~0777U)) {
		result = -EKEYREJECTED;
		goto out;
	}
	result = wmcs_identity_hash(&serialized[BACKUP_HEADER_SIZE], data_size,
				    digest);
	if (result || memcmp(digest, &serialized[52], sizeof(digest))) {
		result = result ? result : -EKEYREJECTED;
		goto out;
	}
	record->data = malloc(data_size ? data_size : 1U);
	if (!record->data) {
		result = -ENOMEM;
		goto out;
	}
	memcpy(record->data, &serialized[BACKUP_HEADER_SIZE], data_size);
	record->data_size = data_size;
	memcpy(record->digest, digest, sizeof(record->digest));
	result = 0;

out:
	wmcs_secure_zero(digest, sizeof(digest));
	wmcs_secure_zero(serialized, serialized_size);
	free(serialized);
	if (result)
		backup_record_clear(record);
	return result;
}

static int backup_remove(const char *state_dir)
{
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	int result = backup_path(path, state_dir);

	if (result)
		return result;
	if (unlink(path) && errno != ENOENT)
		return -errno;
	return wmcs_atomic_file_sync_directory(state_dir);
}

static int restore_record(const struct backup_record *record)
{
	return wmcs_atomic_file_write("/etc/config", WIRELESS_PATH,
				       record->data, record->data_size, record->mode);
}

static bool option_equals(struct uci_context *ctx, struct uci_section *section,
			  const char *name, const char *expected)
{
	const char *value = uci_lookup_option_string(ctx, section, name);

	return value && !strcmp(value, expected);
}

static int validate_profile(const struct wmcs_wlan_request *request)
{
	uint8_t payload[WMCS_CONTROL_PAYLOAD_SIZE];
	bool valid = wmcs_wlan_request_encode(payload, request);

	wmcs_secure_zero(payload, sizeof(payload));
	return valid ? 0 : -EINVAL;
}

int wmcs_wlan_read_source(const char *source_iface, const char *source_radio,
			  struct wmcs_wlan_request *request)
{
	struct uci_context *ctx = NULL;
	struct uci_package *package = NULL;
	struct uci_section *section;
	const char *ssid;
	const char *key;
	int result = -EINVAL;

	if (!source_iface || !source_radio || !request)
		return -EINVAL;
	memset(request, 0, sizeof(*request));
	ctx = uci_alloc_context();
	if (!ctx)
		return -ENOMEM;
	if (uci_load(ctx, "wireless", &package) != UCI_OK)
		goto out;
	section = uci_lookup_section(ctx, package, source_iface);
	if (!section || strcmp(section->type, "wifi-iface") ||
	    !option_equals(ctx, section, "mode", "ap") ||
	    !option_equals(ctx, section, "device", source_radio) ||
	    !option_equals(ctx, section, "network", "lan") ||
	    !option_equals(ctx, section, "encryption", "sae-mixed") ||
	    option_equals(ctx, section, "disabled", "1"))
		goto out;
	ssid = uci_lookup_option_string(ctx, section, "ssid");
	key = uci_lookup_option_string(ctx, section, "key");
	if (!ssid || !key || strlen(ssid) > WMCS_WLAN_SSID_MAX ||
	    strlen(key) > WMCS_WLAN_KEY_MAX)
		goto out;
	request->encryption = WMCS_CONTROL_ENCRYPTION_SAE_MIXED;
	strcpy(request->ssid, ssid);
	strcpy(request->key, key);
	result = validate_profile(request);

out:
	if (package)
		uci_unload(ctx, package);
	uci_free_context(ctx);
	if (result)
		wmcs_secure_zero(request, sizeof(*request));
	return result;
}

int wmcs_wlan_check_target(const char *target_radio, const char *owner_peer_id,
			   const struct wmcs_wlan_request *request)
{
	struct uci_context *ctx = NULL;
	struct uci_package *package = NULL;
	struct uci_section *radio;
	struct uci_section *managed;
	int result;

	if (!target_radio || !peer_id_valid(owner_peer_id) ||
	    validate_profile(request))
		return -EINVAL;
	ctx = uci_alloc_context();
	if (!ctx)
		return -ENOMEM;
	if (uci_load(ctx, "wireless", &package) != UCI_OK) {
		result = -EIO;
		goto out;
	}
	radio = uci_lookup_section(ctx, package, target_radio);
	if (!radio || strcmp(radio->type, "wifi-device") ||
	    !option_equals(ctx, radio, "band", "5g")) {
		result = -ENODEV;
		goto out;
	}
	managed = uci_lookup_section(ctx, package, WMCS_WLAN_SECTION);
	if (managed &&
	    (strcmp(managed->type, "wifi-iface") ||
	     !option_equals(ctx, managed, "wmcs_managed", "1") ||
	     !option_equals(ctx, managed, "wmcs_owner", owner_peer_id))) {
		result = -EEXIST;
		goto out;
	}
	result = 0;

out:
	if (package)
		uci_unload(ctx, package);
	uci_free_context(ctx);
	return result;
}

int wmcs_wlan_check_release(const char *target_radio,
			    const char *owner_peer_id,
			    bool *managed_present)
{
	struct uci_context *ctx = NULL;
	struct uci_package *package = NULL;
	struct uci_section *section;
	int result = 0;

	if (!target_radio || !peer_id_valid(owner_peer_id) || !managed_present)
		return -EINVAL;
	*managed_present = false;
	ctx = uci_alloc_context();
	if (!ctx)
		return -ENOMEM;
	if (uci_load(ctx, "wireless", &package) != UCI_OK) {
		result = -EIO;
		goto out;
	}
	section = uci_lookup_section(ctx, package, WMCS_WLAN_SECTION);
	if (!section)
		goto out;
	if (strcmp(section->type, "wifi-iface") ||
	    !option_equals(ctx, section, "wmcs_managed", "1") ||
	    !option_equals(ctx, section, "wmcs_owner", owner_peer_id) ||
	    !option_equals(ctx, section, "device", target_radio)) {
		result = -EEXIST;
		goto out;
	}
	*managed_present = true;

out:
	if (package)
		uci_unload(ctx, package);
	uci_free_context(ctx);
	return result;
}

static int set_option(struct uci_context *ctx, struct uci_package *package,
		      struct uci_section *section, const char *name,
		      const char *value)
{
	struct uci_ptr pointer = {
		.p = package,
		.s = section,
		.option = name,
		.value = value,
	};

	return uci_set(ctx, &pointer) == UCI_OK ? 0 : -EIO;
}

static int apply_uci(const char *target_radio, const char *owner_peer_id,
		     const struct wmcs_wlan_request *request)
{
	struct uci_context *ctx = NULL;
	struct uci_package *package = NULL;
	struct uci_section *section;
	struct uci_ptr pointer;
	int result = -EIO;

	ctx = uci_alloc_context();
	if (!ctx)
		return -ENOMEM;
	if (uci_load(ctx, "wireless", &package) != UCI_OK)
		goto out;
	section = uci_lookup_section(ctx, package, WMCS_WLAN_SECTION);
	if (!section) {
		if (uci_add_section(ctx, package, "wifi-iface", &section) != UCI_OK)
			goto out;
		memset(&pointer, 0, sizeof(pointer));
		pointer.p = package;
		pointer.s = section;
		pointer.value = WMCS_WLAN_SECTION;
		if (uci_rename(ctx, &pointer) != UCI_OK)
			goto out;
	}
	if (set_option(ctx, package, section, "device", target_radio) ||
	    set_option(ctx, package, section, "network", "lan") ||
	    set_option(ctx, package, section, "mode", "ap") ||
	    set_option(ctx, package, section, "ssid", request->ssid) ||
	    set_option(ctx, package, section, "encryption", "sae-mixed") ||
	    set_option(ctx, package, section, "key", request->key) ||
	    set_option(ctx, package, section, "disabled", "0") ||
	    set_option(ctx, package, section, "wmcs_managed", "1") ||
	    set_option(ctx, package, section, "wmcs_owner", owner_peer_id))
		goto out;
	if (uci_commit(ctx, &package, false) != UCI_OK)
		goto out;
	result = 0;

out:
	if (package)
		uci_unload(ctx, package);
	uci_free_context(ctx);
	return result;
}

static int delete_owned_uci(const char *target_radio,
			    const char *owner_peer_id)
{
	struct uci_context *ctx = NULL;
	struct uci_package *package = NULL;
	struct uci_section *section;
	struct uci_ptr pointer = {0};
	int result = -EIO;

	ctx = uci_alloc_context();
	if (!ctx)
		return -ENOMEM;
	if (uci_load(ctx, "wireless", &package) != UCI_OK)
		goto out;
	section = uci_lookup_section(ctx, package, WMCS_WLAN_SECTION);
	if (!section) {
		result = 0;
		goto out;
	}
	if (strcmp(section->type, "wifi-iface") ||
	    !option_equals(ctx, section, "wmcs_managed", "1") ||
	    !option_equals(ctx, section, "wmcs_owner", owner_peer_id) ||
	    !option_equals(ctx, section, "device", target_radio)) {
		result = -EEXIST;
		goto out;
	}
	pointer.p = package;
	pointer.s = section;
	if (uci_delete(ctx, &pointer) != UCI_OK ||
	    uci_commit(ctx, &package, false) != UCI_OK)
		goto out;
	result = 0;

out:
	if (package)
		uci_unload(ctx, package);
	uci_free_context(ctx);
	return result;
}

static int invoke(struct ubus_context *ctx, const char *object,
		  const char *method, const char *device)
{
	struct blob_buf request = {0};
	uint32_t id;
	int result;

	if (ubus_lookup_id(ctx, object, &id))
		return -ENOENT;
	blob_buf_init(&request, 0);
	if (device)
		blobmsg_add_string(&request, "device", device);
	result = ubus_invoke(ctx, id, method, request.head, NULL, NULL, 5000);
	blob_buf_free(&request);
	return result ? -EIO : 0;
}

static int reload_radio(struct ubus_context *ctx, const char *target_radio)
{
	int result = invoke(ctx, "network", "reload", NULL);

	if (!result)
		result = invoke(ctx, "network.wireless", "reconf", target_radio);
	return result;
}

static int verify_uci(const char *target_radio, const char *owner_peer_id,
		      const struct wmcs_wlan_request *request)
{
	struct uci_context *ctx = NULL;
	struct uci_package *package = NULL;
	struct uci_section *section;
	int result = -EIO;

	ctx = uci_alloc_context();
	if (!ctx)
		return -ENOMEM;
	if (uci_load(ctx, "wireless", &package) != UCI_OK)
		goto out;
	section = uci_lookup_section(ctx, package, WMCS_WLAN_SECTION);
	if (section && !strcmp(section->type, "wifi-iface") &&
	    option_equals(ctx, section, "device", target_radio) &&
	    option_equals(ctx, section, "network", "lan") &&
	    option_equals(ctx, section, "mode", "ap") &&
	    option_equals(ctx, section, "ssid", request->ssid) &&
	    option_equals(ctx, section, "encryption", "sae-mixed") &&
	    option_equals(ctx, section, "key", request->key) &&
	    option_equals(ctx, section, "disabled", "0") &&
	    option_equals(ctx, section, "wmcs_managed", "1") &&
	    option_equals(ctx, section, "wmcs_owner", owner_peer_id))
		result = 0;

out:
	if (package)
		uci_unload(ctx, package);
	uci_free_context(ctx);
	return result;
}

struct wireless_status_result {
	const char *target_radio;
	const char *target_section;
	char ifname[IF_NAMESIZE];
	bool reply_seen;
};

static void wireless_status_callback(struct ubus_request *request, int type,
				     struct blob_attr *message)
{
	struct wireless_status_result *result = request->priv;
	struct blob_attr *device;
	size_t device_remaining;

	(void)type;
	result->reply_seen = true;
	blobmsg_for_each_attr(device, message, device_remaining) {
		struct blob_attr *field;
		size_t field_remaining;

		if (blobmsg_type(device) != BLOBMSG_TYPE_TABLE ||
		    strcmp(blobmsg_name(device), result->target_radio))
			continue;
		blobmsg_for_each_attr(field, device, field_remaining) {
			struct blob_attr *entry;
			size_t entry_remaining;

			if (blobmsg_type(field) != BLOBMSG_TYPE_ARRAY ||
			    strcmp(blobmsg_name(field), "interfaces"))
				continue;
			blobmsg_for_each_attr(entry, field, entry_remaining) {
				struct blob_attr *property;
				const char *section = NULL;
				const char *ifname = NULL;
				size_t property_remaining;

				if (blobmsg_type(entry) != BLOBMSG_TYPE_TABLE)
					continue;
				blobmsg_for_each_attr(property, entry,
						      property_remaining) {
					if (blobmsg_type(property) != BLOBMSG_TYPE_STRING)
						continue;
					if (!strcmp(blobmsg_name(property), "section"))
						section = blobmsg_get_string(property);
					else if (!strcmp(blobmsg_name(property), "ifname"))
						ifname = blobmsg_get_string(property);
				}
				if (section && ifname &&
				    !strcmp(section, result->target_section) &&
				    strlen(ifname) < sizeof(result->ifname)) {
					strcpy(result->ifname, ifname);
					return;
				}
			}
		}
	}
}

static int query_wireless_ifname(struct ubus_context *ctx,
				 const char *target_radio,
				 const char *target_section,
				 char ifname[IF_NAMESIZE])
{
	struct wireless_status_result status = {
		.target_radio = target_radio,
		.target_section = target_section,
	};
	struct blob_buf request = {0};
	uint32_t id;
	int result;

	if (ubus_lookup_id(ctx, "network.wireless", &id))
		return -ENOENT;
	blob_buf_init(&request, 0);
	blobmsg_add_string(&request, "device", target_radio);
	result = ubus_invoke(ctx, id, "status", request.head,
			     wireless_status_callback, &status, 3000);
	blob_buf_free(&request);
	if (result || !status.reply_seen)
		return -EIO;
	if (!status.ifname[0])
		return -ENOENT;
	strcpy(ifname, status.ifname);
	return 0;
}

static int verify_release_uci(void)
{
	struct uci_context *ctx = NULL;
	struct uci_package *package = NULL;
	int result = -EIO;

	ctx = uci_alloc_context();
	if (!ctx)
		return -ENOMEM;
	if (uci_load(ctx, "wireless", &package) != UCI_OK)
		goto out;
	result = uci_lookup_section(ctx, package, WMCS_WLAN_SECTION) ? -EEXIST : 0;

out:
	if (package)
		uci_unload(ctx, package);
	uci_free_context(ctx);
	return result;
}

static void wlan_async_timer_expired(struct uloop_timeout *timeout);
static void wlan_async_ubus_data(struct ubus_request *request, int type,
				 struct blob_attr *message);
static void wlan_async_ubus_complete(struct ubus_request *request, int result);

static void wlan_async_clear_status(struct wmcs_wlan_async *operation)
{
	operation->status_reply_seen = false;
	memset(operation->status_ifname, 0,
	       sizeof(operation->status_ifname));
}

static void wlan_async_schedule_verify(struct wmcs_wlan_async *operation)
{
	operation->phase = WMCS_WLAN_ASYNC_VERIFY;
	operation->verify_deadline_ms = wmcs_monotonic_ms() +
		WMCS_WLAN_VERIFY_TIMEOUT_MS;
	uloop_timeout_set(&operation->timer, 0);
}

static bool wlan_async_hostapd_present(struct wmcs_wlan_async *operation)
{
	char object[IF_NAMESIZE + 16U];
	uint32_t id;

	if (!operation->status_ifname[0] ||
	    snprintf(object, sizeof(object), "hostapd.%s",
		     operation->status_ifname) < 0)
		return false;
	return !ubus_lookup_id(operation->ubus, object, &id);
}

static void wlan_async_finish(struct wmcs_wlan_async *operation, int status,
			       enum wmcs_control_reason reason,
			       bool backup_pending)
{
	wmcs_wlan_async_complete_cb complete;
	void *private;

	if (!operation || !operation->active)
		return;
	uloop_timeout_cancel(&operation->timer);
	if (operation->ubus_pending) {
		operation->ubus_request.complete_cb = NULL;
		operation->ubus_request.data_cb = NULL;
		ubus_abort_request(operation->ubus, &operation->ubus_request);
		operation->ubus_pending = false;
	}
	operation->active = false;
	operation->phase = WMCS_WLAN_ASYNC_IDLE;
	operation->backup_pending = backup_pending;
	complete = operation->complete;
	private = operation->private;
	if (complete)
		complete(private, status, reason, backup_pending);
}

static int wlan_async_invoke(struct wmcs_wlan_async *operation,
			     const char *object, const char *method,
			     const char *device)
{
	struct blob_buf request = {0};
	uint32_t id;
	int result;

	if (ubus_lookup_id(operation->ubus, object, &id))
		return -ENOENT;
	blob_buf_init(&request, 0);
	if (device)
		blobmsg_add_string(&request, "device", device);
	result = ubus_invoke_async(operation->ubus, id, method, request.head,
				   &operation->ubus_request);
	blob_buf_free(&request);
	if (result)
		return -EIO;
	operation->ubus_request.priv = operation;
	operation->ubus_request.data_cb =
		operation->phase == WMCS_WLAN_ASYNC_VERIFY ?
			wlan_async_ubus_data : NULL;
	operation->ubus_request.complete_cb = wlan_async_ubus_complete;
	operation->ubus_pending = true;
	ubus_complete_request_async(operation->ubus, &operation->ubus_request);
	return 0;
}

static int wlan_async_start_reload(struct wmcs_wlan_async *operation,
				   bool rollback)
{
	int result;

	operation->phase = rollback ? WMCS_WLAN_ASYNC_ROLLBACK_RELOAD :
				      WMCS_WLAN_ASYNC_RELOAD;
	result = wlan_async_invoke(operation, "network", "reload", NULL);
	return result;
}

static int wlan_async_start_reconf(struct wmcs_wlan_async *operation,
				   bool rollback)
{
	int result;

	operation->phase = rollback ? WMCS_WLAN_ASYNC_ROLLBACK_RECONF :
				      WMCS_WLAN_ASYNC_RECONF;
	result = wlan_async_invoke(operation, "network.wireless", "reconf",
				   operation->target_radio);
	return result;
}

static void wlan_async_finish_rollback(struct wmcs_wlan_async *operation)
{
	int result = backup_remove(operation->state_dir);

	if (result) {
		wlan_async_finish(operation, -EREMOTEIO,
				  WMCS_CONTROL_REASON_INTERNAL, true);
		return;
	}
	wlan_async_finish(operation, operation->original_status,
			  operation->original_reason, false);
}

static void wlan_async_begin_rollback(struct wmcs_wlan_async *operation,
				      int status,
				      enum wmcs_control_reason reason)
{
	struct backup_record record;
	int result;

	if (operation->rollback_started) {
		wlan_async_finish(operation, -EREMOTEIO,
				  WMCS_CONTROL_REASON_INTERNAL, true);
		return;
	}
	operation->rollback_started = true;
	operation->original_status = status;
	operation->original_reason = reason;
	result = backup_load(operation->state_dir, &record);
	if (!result)
		result = restore_record(&record);
	backup_record_clear(&record);
	if (result) {
		wlan_async_finish(operation, -EREMOTEIO,
				  WMCS_CONTROL_REASON_INTERNAL, true);
		return;
	}
	result = wlan_async_start_reload(operation, true);
	if (result)
		wlan_async_finish(operation, -EREMOTEIO,
				  WMCS_CONTROL_REASON_INTERNAL, true);
}

static void wlan_async_schedule_retry(struct wmcs_wlan_async *operation)
{
	uint64_t now = wmcs_monotonic_ms();

	if (now >= operation->verify_deadline_ms) {
		wlan_async_begin_rollback(operation, -ETIMEDOUT,
					  WMCS_CONTROL_REASON_VERIFY_FAILURE);
		return;
	}
	uloop_timeout_set(&operation->timer, WMCS_WLAN_VERIFY_INTERVAL_MS);
}

static void wlan_async_status_complete(struct wmcs_wlan_async *operation)
{
	int result;

	if (!operation->release) {
		if (operation->status_reply_seen &&
		    wlan_async_hostapd_present(operation)) {
			wlan_async_finish(operation, 0, WMCS_CONTROL_REASON_NONE,
					  true);
			return;
		}
		wlan_async_schedule_retry(operation);
		return;
	}

	result = verify_release_uci();
	if (result && result != -EEXIST) {
		wlan_async_begin_rollback(operation, result,
					  WMCS_CONTROL_REASON_PLATFORM_FAILURE);
		return;
	}
	if (!result && !operation->status_ifname[0]) {
		wlan_async_finish(operation, 0, WMCS_CONTROL_REASON_NONE, true);
		return;
	}
	wlan_async_schedule_retry(operation);
}

static void wlan_async_ubus_data(struct ubus_request *request, int type,
				 struct blob_attr *message)
{
	struct wmcs_wlan_async *operation = request->priv;
	struct blob_attr *device;
	size_t device_remaining;

	(void)type;
	if (!operation || !message)
		return;
	operation->status_reply_seen = true;
	blobmsg_for_each_attr(device, message, device_remaining) {
		struct blob_attr *field;
		size_t field_remaining;

		if (blobmsg_type(device) != BLOBMSG_TYPE_TABLE ||
		    strcmp(blobmsg_name(device), operation->target_radio))
			continue;
		blobmsg_for_each_attr(field, device, field_remaining) {
			struct blob_attr *entry;
			size_t entry_remaining;

			if (blobmsg_type(field) != BLOBMSG_TYPE_ARRAY ||
			    strcmp(blobmsg_name(field), "interfaces"))
				continue;
			blobmsg_for_each_attr(entry, field, entry_remaining) {
				struct blob_attr *property;
				const char *section = NULL;
				const char *ifname = NULL;
				size_t property_remaining;

				if (blobmsg_type(entry) != BLOBMSG_TYPE_TABLE)
					continue;
				blobmsg_for_each_attr(property, entry,
						      property_remaining) {
					if (blobmsg_type(property) != BLOBMSG_TYPE_STRING)
						continue;
					if (!strcmp(blobmsg_name(property), "section"))
						section = blobmsg_get_string(property);
					else if (!strcmp(blobmsg_name(property), "ifname"))
						ifname = blobmsg_get_string(property);
				}
				if (section && ifname &&
				    !strcmp(section, WMCS_WLAN_SECTION) &&
				    strlen(ifname) < sizeof(operation->status_ifname)) {
					strcpy(operation->status_ifname, ifname);
					return;
				}
			}
		}
	}
}

static void wlan_async_ubus_complete(struct ubus_request *request, int result)
{
	struct wmcs_wlan_async *operation = request->priv;

	if (!operation || !operation->active)
		return;
	operation->ubus_pending = false;
	if (result) {
		if (operation->phase == WMCS_WLAN_ASYNC_VERIFY) {
			wlan_async_schedule_retry(operation);
			return;
		}
		if (operation->phase == WMCS_WLAN_ASYNC_ROLLBACK_RELOAD ||
		    operation->phase == WMCS_WLAN_ASYNC_ROLLBACK_RECONF) {
			wlan_async_finish(operation, -EREMOTEIO,
					  WMCS_CONTROL_REASON_INTERNAL, true);
			return;
		}
		wlan_async_begin_rollback(operation, -EIO,
					  WMCS_CONTROL_REASON_PLATFORM_FAILURE);
		return;
	}

	switch (operation->phase) {
	case WMCS_WLAN_ASYNC_RELOAD:
		if (wlan_async_start_reconf(operation, false))
			wlan_async_begin_rollback(operation, -EIO,
					  WMCS_CONTROL_REASON_PLATFORM_FAILURE);
		break;
	case WMCS_WLAN_ASYNC_RECONF:
		if (!operation->release &&
		    verify_uci(operation->target_radio, operation->owner_peer_id,
			       &operation->request))
			wlan_async_begin_rollback(operation, -EIO,
					  WMCS_CONTROL_REASON_VERIFY_FAILURE);
		else {
			wlan_async_clear_status(operation);
			wlan_async_schedule_verify(operation);
		}
		break;
	case WMCS_WLAN_ASYNC_VERIFY:
		wlan_async_status_complete(operation);
		break;
	case WMCS_WLAN_ASYNC_ROLLBACK_RELOAD:
		if (wlan_async_start_reconf(operation, true))
			wlan_async_finish(operation, -EREMOTEIO,
					  WMCS_CONTROL_REASON_INTERNAL, true);
		break;
	case WMCS_WLAN_ASYNC_ROLLBACK_RECONF:
		wlan_async_finish_rollback(operation);
		break;
	case WMCS_WLAN_ASYNC_IDLE:
		break;
	}
}

static void wlan_async_timer_expired(struct uloop_timeout *timeout)
{
	struct wmcs_wlan_async *operation = container_of(
		timeout, struct wmcs_wlan_async, timer);

	if (!operation->active || operation->ubus_pending ||
	    operation->phase != WMCS_WLAN_ASYNC_VERIFY)
		return;
	wlan_async_clear_status(operation);
	if (wlan_async_invoke(operation, "network.wireless", "status",
				      operation->target_radio))
		wlan_async_schedule_retry(operation);
}

int wmcs_wlan_async_init(struct wmcs_wlan_async *operation)
{
	if (!operation)
		return -EINVAL;
	memset(operation, 0, sizeof(*operation));
	operation->timer.cb = wlan_async_timer_expired;
	return 0;
}

void wmcs_wlan_async_cancel(struct wmcs_wlan_async *operation)
{
	if (!operation)
		return;
	uloop_timeout_cancel(&operation->timer);
	if (operation->ubus_pending) {
		operation->ubus_request.complete_cb = NULL;
		operation->ubus_request.data_cb = NULL;
		ubus_abort_request(operation->ubus, &operation->ubus_request);
	}
	operation->ubus_pending = false;
	operation->active = false;
	operation->phase = WMCS_WLAN_ASYNC_IDLE;
}

void wmcs_wlan_async_close(struct wmcs_wlan_async *operation)
{
	if (!operation)
		return;
	wmcs_wlan_async_cancel(operation);
	wmcs_secure_zero(operation, sizeof(*operation));
}

static int wlan_async_prepare(struct wmcs_wlan_async *operation,
				      struct ubus_context *ubus,
				      const char *state_dir,
				      const char *target_radio,
				      const char *owner_peer_id,
				      uint64_t sequence,
				      wmcs_wlan_async_complete_cb complete,
				      void *private)
{
	if (!operation || !ubus || !state_dir || !target_radio ||
	    !peer_id_valid(owner_peer_id) || !sequence || !complete ||
	    strlen(state_dir) >= sizeof(operation->state_dir) ||
	    strlen(target_radio) >= sizeof(operation->target_radio))
		return -EINVAL;
	if (operation->active)
		return -EALREADY;
	wmcs_secure_zero(&operation->request, sizeof(operation->request));
	memset(operation->status_ifname, 0, sizeof(operation->status_ifname));
	operation->ubus = ubus;
	strcpy(operation->state_dir, state_dir);
	strcpy(operation->target_radio, target_radio);
	strcpy(operation->owner_peer_id, owner_peer_id);
	operation->sequence = sequence;
	operation->complete = complete;
	operation->private = private;
	operation->release = false;
	operation->rollback_started = false;
	operation->backup_pending = false;
	operation->status_reply_seen = false;
	operation->phase = WMCS_WLAN_ASYNC_IDLE;
	return 0;
}

int wmcs_wlan_async_start_apply(
	struct wmcs_wlan_async *operation, struct ubus_context *ubus,
	const char *state_dir, const char *target_radio, const char *owner_peer_id,
	uint64_t sequence, const struct wmcs_wlan_request *request,
	wmcs_wlan_async_complete_cb complete, void *private)
{
	int result;

	if (!request)
		return -EINVAL;
	result = wlan_async_prepare(operation, ubus, state_dir, target_radio,
					owner_peer_id, sequence, complete, private);
	if (result)
		return result;
	result = wmcs_wlan_check_target(target_radio, owner_peer_id, request);
	if (result)
		return result;
	operation->request = *request;
	result = backup_create(state_dir, owner_peer_id, sequence);
	if (result)
		return result;
	operation->active = true;
	operation->backup_pending = true;
	result = apply_uci(target_radio, owner_peer_id, request);
	if (result) {
		wlan_async_begin_rollback(operation, result,
					  WMCS_CONTROL_REASON_PLATFORM_FAILURE);
		return 0;
	}
	result = wlan_async_start_reload(operation, false);
	if (result)
		wlan_async_begin_rollback(operation, result,
				  WMCS_CONTROL_REASON_PLATFORM_FAILURE);
	return 0;
}

int wmcs_wlan_async_start_release(
	struct wmcs_wlan_async *operation, struct ubus_context *ubus,
	const char *state_dir, const char *target_radio, const char *owner_peer_id,
	uint64_t sequence, wmcs_wlan_async_complete_cb complete, void *private)
{
	bool managed_present;
	int result;

	result = wlan_async_prepare(operation, ubus, state_dir, target_radio,
					owner_peer_id, sequence, complete, private);
	if (result)
		return result;
	result = wmcs_wlan_check_release(target_radio, owner_peer_id,
					 &managed_present);
	if (result)
		return result;
	operation->release = true;
	operation->active = true;
	if (!managed_present) {
		wlan_async_finish(operation, 0, WMCS_CONTROL_REASON_NONE, false);
		return 0;
	}
	result = backup_create(state_dir, owner_peer_id, sequence);
	if (result) {
		operation->active = false;
		return result;
	}
	operation->backup_pending = true;
	result = delete_owned_uci(target_radio, owner_peer_id);
	if (result) {
		wlan_async_begin_rollback(operation, result,
					  WMCS_CONTROL_REASON_PLATFORM_FAILURE);
		return 0;
	}
	result = wlan_async_start_reload(operation, false);
	if (result)
		wlan_async_begin_rollback(operation, result,
				  WMCS_CONTROL_REASON_PLATFORM_FAILURE);
	return 0;
}

bool wmcs_wlan_async_active(const struct wmcs_wlan_async *operation)
{
	return operation && operation->active;
}

int wmcs_wlan_resolve_hostapd(struct ubus_context *ctx,
			      const char *target_radio, const char *section,
			      char *object, size_t object_size)
{
	char ifname[IF_NAMESIZE];
	uint32_t id;
	int length;

	if (!ctx || !target_radio || !section || !*section || !object ||
	    !object_size)
		return -EINVAL;
	if (query_wireless_ifname(ctx, target_radio, section, ifname))
		return -ENOENT;
	length = snprintf(object, object_size, "hostapd.%s", ifname);
	if (length < 0 || (size_t)length >= object_size)
		return -ENAMETOOLONG;
	if (ubus_lookup_id(ctx, object, &id))
		return -ENOENT;
	return 0;
}

int wmcs_wlan_abort(struct ubus_context *ubus, const char *state_dir,
		    const char *target_radio)
{
	struct backup_record record;
	int result;
	int reload_result;

	result = backup_load(state_dir, &record);
	if (result)
		return result;
	result = restore_record(&record);
	reload_result = result ? 0 : reload_radio(ubus, target_radio);
	if (!result && reload_result)
		result = reload_result;
	if (!result)
		result = backup_remove(state_dir);
	backup_record_clear(&record);
	return result;
}

int wmcs_wlan_finish(const char *state_dir)
{
	return backup_remove(state_dir);
}

int wmcs_wlan_recover(struct ubus_context *ubus, struct wmcs_identity *identity,
		      const char *state_dir, const char *target_radio)
{
	struct backup_record record;
	struct wmcs_peer_record peer;
	int result;

	result = backup_load(state_dir, &record);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	result = wmcs_identity_load_peer(identity, record.peer_id, &peer);
	if (!result && peer.generation >= record.sequence)
		result = backup_remove(state_dir);
	else
		result = wmcs_wlan_abort(ubus, state_dir, target_radio);
	wmcs_secure_zero(&peer, sizeof(peer));
	backup_record_clear(&record);
	return result;
}

int wmcs_wlan_peer_has_state(const char *state_dir, const char *peer_id,
			     bool *has_state)
{
	struct uci_context *ctx = NULL;
	struct uci_package *package = NULL;
	struct uci_section *section;
	struct backup_record record;
	int result;

	if (!state_dir || !peer_id_valid(peer_id) || !has_state)
		return -EINVAL;
	*has_state = false;
	ctx = uci_alloc_context();
	if (!ctx)
		return -ENOMEM;
	if (uci_load(ctx, "wireless", &package) != UCI_OK) {
		result = -EIO;
		goto out_uci;
	}
	section = uci_lookup_section(ctx, package, WMCS_WLAN_SECTION);
	if (section && option_equals(ctx, section, "wmcs_owner", peer_id))
		*has_state = true;
	result = 0;

out_uci:
	if (package)
		uci_unload(ctx, package);
	uci_free_context(ctx);
	if (result || *has_state)
		return result;

	result = backup_load(state_dir, &record);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	*has_state = !strcmp(record.peer_id, peer_id);
	backup_record_clear(&record);
	return 0;
}
