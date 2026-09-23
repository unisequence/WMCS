// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <stdio.h>

#include <libubox/blobmsg.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>

static struct blob_buf reply;

static int status(struct ubus_context *ctx, struct ubus_object *object,
		  struct ubus_request_data *request, const char *method,
		  struct blob_attr *message)
{
	(void)object;
	(void)method;
	(void)message;

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", 0);
	blobmsg_add_string(&reply, "runtime", "c");
	blobmsg_add_string(&reply, "state", "spike");

	return ubus_send_reply(ctx, request, reply.head);
}

static const struct ubus_method methods[] = {
	UBUS_METHOD_NOARG("status", status),
};

static struct ubus_object_type object_type =
	UBUS_OBJECT_TYPE("wmcs.spike", methods);

static struct ubus_object object = {
	.name = "wmcs.spike",
	.type = &object_type,
	.methods = methods,
	.n_methods = ARRAY_SIZE(methods),
};

int main(void)
{
	struct ubus_context *ctx;
	int error;

	uloop_init();
	ctx = ubus_connect(NULL);
	if (!ctx) {
		fprintf(stderr, "wmcs-spike: cannot connect to ubus\n");
		uloop_done();
		return 1;
	}

	ubus_add_uloop(ctx);
	error = ubus_add_object(ctx, &object);
	if (error) {
		fprintf(stderr, "wmcs-spike: cannot register object: %s\n",
			ubus_strerror(error));
		ubus_free(ctx);
		uloop_done();
		return 1;
	}

	fprintf(stderr, "wmcs-spike: registered wmcs.spike\n");
	uloop_run();

	ubus_free(ctx);
	uloop_done();
	blob_buf_free(&reply);

	return 0;
}
