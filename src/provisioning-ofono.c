/*
 *  Copyright (C) 2014-2017 Jolla Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <gofono_manager.h>
#include <gofono_simmgr.h>
#include <gofono_connmgr.h>
#include <gofono_connctx.h>
#include <gofono_modem.h>
#include <gofono_names.h>

#include <gio/gio.h>

#include "log.h"
#include "provisioning-ofono.h"
#include "provisioning-decoder.h"

#define PROVISIONING_TIMEOUT 30 /* sec */

enum provisioning_context_state {
	PROV_CONTEXT_INITIALIZING,
	PROV_CONTEXT_DEACTIVATING,
	PROV_CONTEXT_PROVISIONING,
	/* Final states must be the last: */
	PROV_CONTEXT_SUCCESS,
	PROV_CONTEXT_ERROR
};

enum provisioning_context_property {
	PROV_PROPERTY_NAME,
	PROV_PROPERTY_APN,
	PROV_PROPERTY_USERNAME,
	PROV_PROPERTY_PASSWORD,
	PROV_PROPERTY_AUTH,
	PROV_PROPERTY_INTERNET_COUNT,
	/* Below are MMS specific properties */
	PROV_PROPERTY_MMS_PROXY = PROV_PROPERTY_INTERNET_COUNT,
	PROV_PROPERTY_MMS_CENTER,
	PROV_PROPERTY_MMS_COUNT
};

struct provisioning_ofono {
	char *imsi;
	struct provisioning_data *data;
	OfonoManager *manager;
	gulong manager_valid_id;
	guint timeout_id;
	provisioning_ofono_cb_t done;
	void *param;
	GSList *sim_list;
};

struct provisioning_sim {
	OfonoSimMgr *simmgr;
	OfonoConnMgr* connmgr;
	gulong simmgr_valid_id;
	gulong connmgr_valid_id;
	struct provisioning_ofono *ofono;
	struct provisioning_context *internet;
	struct provisioning_context *mms;
};

struct provisioning_context {
	int refcount;
	OfonoConnCtx* connctx;
	gulong connctx_valid_id;
	gulong connctx_active_id;
	struct provisioning_sim *sim;
	enum provisioning_context_state state;
	int outstanding_requests;
	GCancellable **req;
	int nreq;
	void (*set_properties)(struct provisioning_context *ctx);
};

struct provisioning_property_request {
	struct provisioning_context *ctx;
	const char *name;
	int index;
};

static
struct provisioning_context*
provisioning_context_ref(
	struct provisioning_context *ctx)
{
	if (ctx) {
		ctx->refcount++;
	}
	return ctx;
}

static
void
provisioning_context_unref(
	struct provisioning_context *ctx)
{
	if (ctx && !--ctx->refcount) {
		ofono_connctx_remove_handler(ctx->connctx, ctx->connctx_valid_id);
		ofono_connctx_remove_handler(ctx->connctx, ctx->connctx_active_id);
		ofono_connctx_unref(ctx->connctx);
		g_free(ctx->req);
		g_free(ctx);
	}
}

static
void
provisioning_context_cancel(
	struct provisioning_context *context)
{
	if (context) {
		int i;
		for (i=0; i<context->nreq; i++) {
			if (context->req[i]) {
				g_cancellable_cancel(context->req[i]);
				g_object_unref(context->req[i]);
				context->req[i] = NULL;
			}
		}
		context->sim = NULL;
		provisioning_context_unref(context);
	}
}

static
void
provisioning_ofono_free_sim(
	gpointer arg)
{
	struct provisioning_sim *sim = arg;
	provisioning_context_cancel(sim->internet);
	provisioning_context_cancel(sim->mms);
	ofono_connmgr_remove_handler(sim->connmgr, sim->connmgr_valid_id);
	ofono_simmgr_remove_handler(sim->simmgr, sim->simmgr_valid_id);
	ofono_connmgr_unref(sim->connmgr);
	ofono_simmgr_unref(sim->simmgr);
	g_free(sim);
}

static
void
provisioning_ofono_done(
	struct provisioning_ofono *ofono,
	const char *path,
	enum prov_result result)
{
	if (ofono->done) {
		ofono->done(ofono->imsi, path, result, ofono->param);
		ofono->done = NULL;
	}
	if (ofono->timeout_id) {
		g_source_remove(ofono->timeout_id);
	}
	ofono_manager_remove_handler(ofono->manager, ofono->manager_valid_id);
	ofono_manager_unref(ofono->manager);
	g_slist_free_full(ofono->sim_list, provisioning_ofono_free_sim);
	provisioning_data_free(ofono->data);
	g_free(ofono->imsi);
	g_free(ofono);
}

static
gboolean
provisioning_ofono_timeout(
	gpointer data)
{
	struct provisioning_ofono *ofono = data;
	LOG("Timeout trying to provision %s", ofono->imsi);
	ofono->timeout_id = 0;
	provisioning_ofono_done(ofono, NULL, PROV_FAILURE);
	return FALSE;
}

static
void
provisioning_sim_check(
	struct provisioning_sim *sim)
{
	if ((!sim->internet || sim->internet->state > PROV_CONTEXT_PROVISIONING) &&
	    (!sim->mms || sim->mms->state > PROV_CONTEXT_PROVISIONING)) {
		/* All done */
		int context_count = 0, success_count = 0, error_count = 0;
		if (sim->internet) {
			context_count++;
			if (sim->internet->state == PROV_CONTEXT_SUCCESS) {
				success_count++;
			} else {
				error_count++;
			}
		}
		if (sim->mms) {
			context_count++;
			if (sim->mms->state == PROV_CONTEXT_SUCCESS) {
				success_count++;
			} else {
				error_count++;
			}
		}
		provisioning_ofono_done(sim->ofono,
			ofono_simmgr_path(sim->simmgr),
			(context_count && success_count == context_count) ? PROV_SUCCESS :
			(error_count == context_count) ? PROV_FAILURE :
			PROV_PARTIAL_SUCCESS);
	}
}

static
void
provisioning_property_request_done(
	OfonoConnCtx *connctx,
	const GError *error,
	void *data)
{
	struct provisioning_property_request *prop = data;
	struct provisioning_context *ctx = prop->ctx;
	ctx->outstanding_requests--;
	LOG("%s (%d) %s %s", ofono_connctx_path(connctx), ctx->outstanding_requests,
		prop->name, error ? error->message : "OK");
	GASSERT(ctx->req[prop->index]);
	g_object_unref(ctx->req[prop->index]);
	ctx->req[prop->index] = NULL;
	if (error) {
		ctx->state = PROV_CONTEXT_ERROR;
	}
	if (!ctx->outstanding_requests) {
		/* Last request has been completed */
		if (ctx->state == PROV_CONTEXT_PROVISIONING) {
			ctx->state = PROV_CONTEXT_SUCCESS;
		}
		provisioning_sim_check(ctx->sim);
	}
	provisioning_context_unref(ctx);
	g_free(prop);
}

static
void
provisioning_context_request_submit(
	struct provisioning_context *ctx,
	int index,
	const char *name,
	const char *value)
{
	struct provisioning_property_request *prop;
	GASSERT(!ctx->req[index]);
	if (!value) value = "";
	prop = g_new(struct provisioning_property_request, 1);
	prop->ctx = provisioning_context_ref(ctx);
	prop->index = index;
	prop->name = name;
	ctx->outstanding_requests++;
	LOG("%s (%d) %s = \"%s\"", ofono_connctx_path(ctx->connctx),
		ctx->outstanding_requests, name, value);
	ctx->req[index] = ofono_connctx_set_string_full(ctx->connctx,
		name, value, provisioning_property_request_done, prop);
	g_object_ref(ctx->req[index]);
}

static
const char *
provisioning_context_auth_string(
	enum prov_authtype authtype,
	const char *username,
	const char *password)
{
	OFONO_CONNCTX_AUTH auth;
	if ((!username || !username[0]) && (!password || !password[0])) {
		/* No username or password */
		auth = OFONO_CONNCTX_AUTH_NONE;
	} else if (authtype == AUTH_PAP) {
		auth = OFONO_CONNCTX_AUTH_PAP;
	} else if (authtype == AUTH_CHAP) {
		auth = OFONO_CONNCTX_AUTH_CHAP;
	} else {
		auth = OFONO_CONNCTX_AUTH_ANY;
	}
	return ofono_connctx_auth_string(auth);
}

static
void
provisioning_context_set_internet_properties(
	struct provisioning_context *ctx)
{
	const struct provisioning_internet *internet =
		ctx->sim->ofono->data->internet;
	if (internet) {
		provisioning_context_request_submit(ctx, PROV_PROPERTY_NAME,
			OFONO_CONNCTX_PROPERTY_NAME, internet->name);
		provisioning_context_request_submit(ctx, PROV_PROPERTY_APN,
			OFONO_CONNCTX_PROPERTY_APN, internet->apn);
		provisioning_context_request_submit(ctx, PROV_PROPERTY_USERNAME,
			OFONO_CONNCTX_PROPERTY_USERNAME, internet->username);
		provisioning_context_request_submit(ctx, PROV_PROPERTY_PASSWORD,
			OFONO_CONNCTX_PROPERTY_PASSWORD, internet->password);
		provisioning_context_request_submit(ctx, PROV_PROPERTY_AUTH,
			OFONO_CONNCTX_PROPERTY_AUTH, provisioning_context_auth_string(
				internet->authtype, internet->username, internet->password));
	}
}

static
void
provisioning_context_set_mms_properties(
	struct provisioning_context *ctx)
{
	const struct provisioning_mms *mms = ctx->sim->ofono->data->mms;
	if (mms) {
		provisioning_context_request_submit(ctx, PROV_PROPERTY_NAME,
			OFONO_CONNCTX_PROPERTY_NAME, mms->name);
		provisioning_context_request_submit(ctx, PROV_PROPERTY_APN,
			OFONO_CONNCTX_PROPERTY_APN, mms->apn);
		provisioning_context_request_submit(ctx, PROV_PROPERTY_USERNAME,
			OFONO_CONNCTX_PROPERTY_USERNAME, mms->username);
		provisioning_context_request_submit(ctx, PROV_PROPERTY_PASSWORD,
			OFONO_CONNCTX_PROPERTY_PASSWORD, mms->password);
		provisioning_context_request_submit(ctx, PROV_PROPERTY_AUTH,
			OFONO_CONNCTX_PROPERTY_AUTH, provisioning_context_auth_string(
				mms->authtype, mms->username, mms->password));
		provisioning_context_request_submit(ctx, PROV_PROPERTY_MMS_CENTER,
			OFONO_CONNCTX_PROPERTY_MMS_CENTER, mms->messagecenter);
		if (mms->messageproxy && mms->messageproxy[0] &&
		    mms->portnro && mms->portnro[0]) {
			char *val = g_strconcat(mms->messageproxy, ":", mms->portnro, NULL);
			provisioning_context_request_submit(ctx, PROV_PROPERTY_MMS_PROXY,
				OFONO_CONNCTX_PROPERTY_MMS_PROXY, val);
			g_free(val);
		} else {
			provisioning_context_request_submit(ctx, PROV_PROPERTY_MMS_PROXY,
				OFONO_CONNCTX_PROPERTY_MMS_PROXY, mms->messageproxy);
		}
	}
}

static
void
provisioning_context_inactive(
	struct provisioning_context *ctx)
{
	ctx->set_properties(ctx);
	ctx->state = ctx->outstanding_requests ?
		PROV_CONTEXT_PROVISIONING :
		PROV_CONTEXT_ERROR;
}

static
void
provisioning_context_active_changed(
	OfonoConnCtx *connctx,
	void *arg)
{
	struct provisioning_context *ctx = arg;
	if (ofono_connctx_valid(connctx) && !connctx->active) {
		ofono_connctx_remove_handler(ctx->connctx, ctx->connctx_valid_id);
		ofono_connctx_remove_handler(ctx->connctx, ctx->connctx_active_id);
		ctx->connctx_valid_id = 0;
		ctx->connctx_active_id = 0;
		provisioning_context_inactive(ctx);
	}
}

static
void
provisioning_context_valid(
	struct provisioning_context *ctx)
{
	LOG("%s active %d", ofono_connctx_path(ctx->connctx), ctx->connctx->active);
	if (ctx->connctx->active) {
		ctx->state = PROV_CONTEXT_DEACTIVATING;
		GASSERT(!ctx->connctx_active_id);
		GASSERT(!ctx->connctx_valid_id);
		ctx->connctx_active_id = ofono_connctx_add_active_changed_handler(
			ctx->connctx, provisioning_context_active_changed, ctx);
		ctx->connctx_valid_id = ofono_connctx_add_valid_changed_handler(
			ctx->connctx, provisioning_context_active_changed, ctx);
		ofono_connctx_deactivate(ctx->connctx);
	} else {
		provisioning_context_inactive(ctx);
	}
}

static
void
provisioning_context_valid_changed(
	OfonoConnCtx *connctx,
	void *arg)
{
	if (ofono_connctx_valid(connctx)) {
		struct provisioning_context *ctx = arg;
		ofono_connctx_remove_handler(connctx, ctx->connctx_valid_id);
		ctx->connctx_valid_id = 0;
		provisioning_context_valid(ctx);
	}
}

static
struct provisioning_context*
provisioning_context_new(
	struct provisioning_sim *sim,
	OfonoConnCtx *connctx,
	void (*set_properties)(struct provisioning_context *ctx),
	int nreq)
{
	struct provisioning_context *ctx = g_new0(struct provisioning_context, 1);
	ctx->refcount = 1;
	ctx->connctx = ofono_connctx_ref(connctx);
	ctx->sim = sim;
	ctx->req = g_new0(GCancellable*, nreq);
	ctx->nreq = nreq;
	ctx->state = PROV_CONTEXT_INITIALIZING;
	ctx->set_properties = set_properties;
	LOG("Configuring %s", ofono_connctx_path(connctx));
	if (ofono_connctx_valid(ctx->connctx)) {
		provisioning_context_valid(ctx);
	} else {
		ctx->connctx_valid_id = ofono_connctx_add_valid_changed_handler(
			ctx->connctx, provisioning_context_valid_changed, ctx);
	}
	return ctx;
}

static
void
provisioning_connmgr_valid(
	struct provisioning_sim *sim)
{
	/* Jolla fork of ofono makes sure that there's always one internet and
	 * one mms context. In a more general case it may be necessary to create
	 * contexts if they don't exist.
	 */
	struct provisioning_ofono *ofono = sim->ofono;
	if (ofono->data->internet &&
	    ofono->data->internet->apn &&
	    ofono->data->internet->apn[0]) {
		OfonoConnCtx *ctx = ofono_connmgr_get_context_for_type(sim->connmgr,
			OFONO_CONNCTX_TYPE_INTERNET);
		if (ctx) {
			sim->internet = provisioning_context_new(sim, ctx,
				provisioning_context_set_internet_properties,
				PROV_PROPERTY_INTERNET_COUNT);
		}
	}
	if (ofono->data->mms &&
	    ofono->data->mms->apn &&
	    ofono->data->mms->apn[0]) {
		OfonoConnCtx *ctx = ofono_connmgr_get_context_for_type(sim->connmgr,
			OFONO_CONNCTX_TYPE_MMS);
		if (ctx) {
			sim->mms = provisioning_context_new(sim, ctx,
				provisioning_context_set_mms_properties,
				PROV_PROPERTY_MMS_COUNT);
		}
	}
	provisioning_sim_check(sim);
}

static
void
provisioning_connmgr_valid_changed(
	OfonoConnMgr *connmgr,
	void *arg)
{
	if (ofono_connmgr_valid(connmgr)) {
		struct provisioning_sim *sim = arg;
		ofono_connmgr_remove_handler(connmgr, sim->connmgr_valid_id);
		sim->connmgr_valid_id = 0;
		provisioning_connmgr_valid(sim);
	}
}

static
void
provisioning_sim_valid(
	struct provisioning_sim *sim)
{
	OfonoSimMgr *simmgr = sim->simmgr;
	LOG("%s -> %s", ofono_simmgr_path(simmgr), simmgr->imsi);
	if (simmgr->present && !g_strcmp0(sim->ofono->imsi, simmgr->imsi)) {
		LOG("Provisioning %s", simmgr->imsi);
		if (ofono_connmgr_valid(sim->connmgr)) {
			provisioning_connmgr_valid(sim);
		} else {
			sim->connmgr_valid_id = ofono_connmgr_add_valid_changed_handler(
				sim->connmgr, provisioning_connmgr_valid_changed, sim);
		}
	} else {
		LOG("%s sim at %s", simmgr->present ? "Wrong" : "No",
			ofono_simmgr_path(simmgr));
	}
}

static
void
provisioning_sim_valid_changed(
	OfonoSimMgr *simmgr,
	void *arg)
{
	if (ofono_simmgr_valid(simmgr)) {
		struct provisioning_sim *sim = arg;
		ofono_simmgr_remove_handler(simmgr, sim->simmgr_valid_id);
		sim->simmgr_valid_id = 0;
		provisioning_sim_valid(sim);
	}
}

static
struct provisioning_sim*
provisioning_sim_new(
	struct provisioning_ofono *ofono,
	OfonoModem *modem)
{
	struct provisioning_sim *sim = g_new0(struct provisioning_sim, 1);
	const char *path = ofono_modem_path(modem);
	sim->simmgr = ofono_simmgr_new(path);
	sim->connmgr = ofono_connmgr_new(path);
	sim->ofono = ofono;
	if (ofono_simmgr_valid(sim->simmgr)) {
		provisioning_sim_valid(sim);
	} else {
		sim->simmgr_valid_id = ofono_simmgr_add_valid_changed_handler(
			sim->simmgr, provisioning_sim_valid_changed, sim);
	}
	return sim;
}

static
void
provisioning_manager_valid(
	struct provisioning_ofono *ofono)
{
	GPtrArray *modems = ofono_manager_get_modems(ofono->manager);
	guint i;
	for (i=0; i<modems->len; i++) {
		ofono->sim_list = g_slist_append(ofono->sim_list,
			provisioning_sim_new(ofono, modems->pdata[i]));
	}
}

static
void
provisioning_manager_valid_changed(
	OfonoManager *manager,
	void *arg)
{
	if (manager->valid) {
		struct provisioning_ofono *ofono = arg;
		ofono_manager_remove_handler(manager, ofono->manager_valid_id);
		ofono->manager_valid_id = 0;
		provisioning_manager_valid(ofono);
	}
}

void
provisioning_ofono(
	const char *imsi,
	struct provisioning_data *data,
	provisioning_ofono_cb_t done,
	void *param)
{
	struct provisioning_ofono *ofono = g_new0(struct provisioning_ofono, 1);
	ofono->imsi = g_strdup(imsi);
	ofono->data = data;
	ofono->done = done;
	ofono->param = param;
	ofono->manager = ofono_manager_new();
	ofono->timeout_id = g_timeout_add_seconds(PROVISIONING_TIMEOUT,
		provisioning_ofono_timeout, ofono);
	if (ofono->manager->valid) {
		provisioning_manager_valid(ofono);
	} else {
		ofono->manager_valid_id = ofono_manager_add_valid_changed_handler(
			ofono->manager, provisioning_manager_valid_changed, ofono);
	}
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
