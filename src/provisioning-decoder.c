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

#include "provisioning-decoder.h"
#include "log.h"

#include <wbxml.h>

#define APPID_INTERNET      "w2"
#define APPID_MMS_1         "w4"
#define APPID_MMS_2         "ap0005"

/* These are exported by libwbxml2 but not defined in any header file */
extern const WBXMLPublicIDEntry sv_prov10_public_id;
extern const WBXMLTagEntry sv_prov10_tag_table[];
extern const WBXMLAttrEntry sv_prov10_attr_table[];
extern const WBXMLAttrValueEntry sv_prov10_attr_value_table[];

static const WBXMLLangEntry prov_table[] = {
	{
		WBXML_LANG_PROV10,
		&sv_prov10_public_id,
		sv_prov10_tag_table,
		NULL,
		sv_prov10_attr_table,
		sv_prov10_attr_value_table,
		NULL
	},{
		WBXML_LANG_UNKNOWN,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	}
};

#define ELEM_CHARACTERISTIC             "characteristic"
#define ELEM_PARM                       "parm"
#define ATTR_TYPE                       "type"
#define ATTR_NAME                       "name"
#define ATTR_VALUE                      "value"

#define TYPE_NAPDEF                     "NAPDEF"
#define TYPE_APPLICATION                "APPLICATION"
#define TYPE_PXLOGICAL                  "PXLOGICAL"

#define PARM_APPID                      "APPID"
#define PARM_TO_NAPID                   "TO-NAPID"
#define PARM_TO_PROXY                   "TO-PROXY"
#define PARM_NAPID                      "NAPID"
#define PARM_PROXY_ID                   "PROXY-ID"
#define PARM_INTERNET                   "INTERNET"
#define PARM_NAME                       "NAME"
#define PARM_NAP_ADDRESS                "NAP-ADDRESS"
#define PARM_NAP_ADDRTYPE               "NAP-ADDRTYPE"
#define PARM_AUTHTYPE                   "AUTHTYPE"
#define PARM_AUTHNAME                   "AUTHNAME"
#define PARM_AUTHSECRET                 "AUTHSECRET"
#define PARM_PXADDR                     "PXADDR"
#define PARM_PORTNBR                    "PORTNBR"
#define PARM_ADDR                       "ADDR"

#define NAP_ADDRTYPE_APN                "APN"

#define AUTHTYPE_PAP                    "PAP"
#define AUTHTYPE_CHAP                   "CHAP"
#define AUTHTYPE_MD5                    "MD5"

struct provisioning_wbxml_context {
	GSList *napdef;                     /* NAPDEF characteristics */
	GSList *application;                /* APPLICATION characteristics */
	GSList *pxlogical;                  /* PXLOGICAL characteristics */
	GHashTable *characteristic;         /* Currently being parsed */
	int characteristic_depth;           /* <characteristic> depth */
};

static
const char*
provisioning_wbxml_attr_value(
	WBXMLAttribute **atts,
	const char *name)
{
	if (atts) {
		while (*atts) {
			if (!g_strcmp0(name, (char*)wbxml_attribute_get_xml_name(*atts))) {
				return (char*)wbxml_attribute_get_xml_value(*atts);
			}
			atts++;
		}
	}
	return NULL;
}

static
void
provisioning_wbxml_start_element(
	void *ctx,
	WBXMLTag *tag,
	WBXMLAttribute **atts,
	WB_BOOL empty)
{
	struct provisioning_wbxml_context *context = ctx;
	const char *elem = (char*)wbxml_tag_get_xml_name(tag);
	if (!g_strcmp0(elem, ELEM_PARM)) {
		/*
		 * If current characteristic is being ignored then
		 * context->characteristic is going to be NULL
		 */
		if (context->characteristic) {
			const char *name = provisioning_wbxml_attr_value(atts, ATTR_NAME);
			const char *value = provisioning_wbxml_attr_value(atts, ATTR_VALUE);
			LOG("  <%s name=\"%s\" value=\"%s\">", elem, name, value);
			if (name && name[0]) {
				g_hash_table_insert(context->characteristic,
					g_strdup(name), g_strdup(value));
			}
		}
	} else if (!g_strcmp0(elem, ELEM_CHARACTERISTIC)) {
		GASSERT(context->characteristic_depth >= 0);
		if (!context->characteristic_depth) {
			/* New top-level characteristic */
			GSList **list = NULL;
			const char *type = provisioning_wbxml_attr_value(atts, ATTR_TYPE);
			if (!g_strcmp0(type, TYPE_NAPDEF)) {
				list = &context->napdef;
			} else if (!g_strcmp0(type, TYPE_APPLICATION)) {
				list = &context->application;
			} else if (!g_strcmp0(type, TYPE_PXLOGICAL)) {
				list = &context->pxlogical;
			}
			if (list) {
				LOG("<%s type=\"%s\">", elem, type);
				context->characteristic = g_hash_table_new_full(g_str_hash,
					g_str_equal, g_free, g_free);
				*list = g_slist_append(*list, context->characteristic);
			} else {
				LOG("<%s type=\"%s\"> IGNORED", elem, type);
			}
		}
		context->characteristic_depth++;
	} else {
		LOG("<%s> IGNORED", elem);
	}
}

static
void
provisioning_wbxml_end_element(
	void *ctx,
	WBXMLTag *tag,
	WB_BOOL empty)
{
	struct provisioning_wbxml_context *context = ctx;
	const char *elem = (char*)wbxml_tag_get_xml_name(tag);
	if (!g_strcmp0(elem, ELEM_CHARACTERISTIC)) {
		GASSERT(context->characteristic_depth > 0);
		context->characteristic_depth--;
		if (!context->characteristic_depth) {
			context->characteristic = NULL;
			LOG("</%s>", wbxml_tag_get_xml_name(tag));
		}
	}
}

static
void
provisioning_wbxml_chars_free(
	gpointer data)
{
	g_hash_table_destroy(data);
}

static
GHashTable*
provisioning_wbxml_chars_find(
	GSList *list,
	const char *key,
	const char *value)
{
	while (list) {
		GHashTable *chars = list->data;
		if (value) {
			const char *found = g_hash_table_lookup(chars, key);
			if (found && !strcmp(found, value)) {
				return chars;
			}
		} else if (g_hash_table_contains(chars, key)) {
			return chars;
		}
		list = list->next;
	}
	return NULL;
}

static
void
provisioning_wbxml_chars_merge(
	GHashTable *dest,
	GHashTable *src)
{
	if (src) {
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, src);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			g_hash_table_insert(dest, key, value);
		}
	}
}

static
enum prov_authtype
provisioning_wbxml_chars_authtype(
	GHashTable *chars)
{
	const char *authtype = g_hash_table_lookup(chars, PARM_AUTHTYPE);
	if (authtype) {
		if (!strcmp(authtype, AUTHTYPE_PAP)) {
			return AUTH_PAP;
		} else if (!strcmp(authtype, AUTHTYPE_CHAP)) {
			return AUTH_CHAP;
		} else if (!strcmp(authtype, AUTHTYPE_MD5)) {
			return AUTH_MD5;
		}
	}
	return AUTH_UNKNOWN;
}

/**
 * This is the main function that actually decides which settings to use.
 * It's not as straighforward as you might have thought.
 */
static
struct provisioning_data*
provisioning_wbxml_context_data(
	struct provisioning_wbxml_context *context)
{
	struct provisioning_data *data = g_new0(struct provisioning_data, 1);
	GHashTable *mms_app, *mms_nap = NULL, *mms_proxy = NULL;
	GHashTable *inet_app, *inet_nap = NULL;

	/*
	 * OMA-WAP-TS-ProvCont-V1_1-20090728-A
	 *
	 * One TO-NAPID has a special predefined meaning. If the TO-NAPID is
	 * INTERNET, it implies that the ME can select any network access point
	 * with the attribute INTERNET defined.
	 */
	inet_app = provisioning_wbxml_chars_find(context->application,
		PARM_TO_NAPID, PARM_INTERNET);
	inet_nap = provisioning_wbxml_chars_find(context->napdef,
		PARM_INTERNET, NULL);
	if (inet_nap && !inet_app) {
		const char *napid = g_hash_table_lookup(inet_nap, PARM_NAPID);
		if (napid) {
			inet_app = provisioning_wbxml_chars_find(context->application,
				PARM_TO_NAPID, napid);
		}
	}
	if (!inet_app) {
		inet_app = provisioning_wbxml_chars_find(context->application,
			PARM_APPID, APPID_INTERNET);
	}
	if (!inet_nap && inet_app) {
		const char *napid = g_hash_table_lookup(inet_app, PARM_TO_NAPID);
		if (napid) {
			inet_nap = provisioning_wbxml_chars_find(context->napdef,
				PARM_NAPID, napid);
			GASSERT(inet_nap);
		}
	}

	mms_app = provisioning_wbxml_chars_find(context->application,
		PARM_APPID, APPID_MMS_1);
	if (!mms_app) {
		mms_app = provisioning_wbxml_chars_find(context->application,
			PARM_APPID, APPID_MMS_2);
	}
	if (mms_app) {
		const char *proxy = g_hash_table_lookup(mms_app, PARM_TO_PROXY);
		const char *napid = g_hash_table_lookup(mms_app, PARM_TO_NAPID);
		if (proxy) {
			mms_proxy = provisioning_wbxml_chars_find(context->pxlogical,
					PARM_PROXY_ID, proxy);
		}
		if (mms_proxy && !napid) {
			napid = g_hash_table_lookup(mms_proxy, PARM_TO_NAPID);
		}
		if (napid) {
			mms_nap = provisioning_wbxml_chars_find(context->napdef,
				PARM_NAPID, napid);
			GASSERT(mms_nap);
		}
	}

	/* Internet context */
	if (inet_nap) {
		const char *apn;
		const char *addr_type;

		/* Merge all characteristics into one table */
		GHashTable *inet = g_hash_table_new(g_str_hash, g_str_equal);
		provisioning_wbxml_chars_merge(inet, inet_app);
		provisioning_wbxml_chars_merge(inet, inet_nap);

#if GUTIL_LOG_DEBUG
		if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
			GHashTableIter iter;
			gpointer key, value;
			LOG("Internet:")
			g_hash_table_iter_init(&iter, inet);
			while (g_hash_table_iter_next(&iter, &key, &value)) {
				LOG("  %s = %s", (char*)key, (char*)value);
			}
		}
#endif

		/* APN is required */
		apn = g_hash_table_lookup(inet, PARM_NAP_ADDRESS);
		addr_type = g_hash_table_lookup(inet, PARM_NAP_ADDRTYPE);
		if (apn && apn[0] && !g_strcmp0(addr_type, NAP_ADDRTYPE_APN)) {
			data->internet = g_new0(struct provisioning_internet, 1);
			data->internet->apn = g_strdup(apn);
			data->internet->authtype = provisioning_wbxml_chars_authtype(inet);
			data->internet->name =
				g_strdup(g_hash_table_lookup(inet, PARM_NAME));
			data->internet->username =
				g_strdup(g_hash_table_lookup(inet, PARM_AUTHNAME));
			data->internet->password =
				g_strdup(g_hash_table_lookup(inet, PARM_AUTHSECRET));
		} else {
			GERR("No internet APN");
		}
		g_hash_table_destroy(inet);
	}

	/* MMS context */
	if (mms_nap) {
		const char *apn;
		const char *addr_type;

		/* Merge all characteristics into one table */
		GHashTable *mms = g_hash_table_new(g_str_hash, g_str_equal);
		provisioning_wbxml_chars_merge(mms, mms_app);
		provisioning_wbxml_chars_merge(mms, mms_proxy);
		provisioning_wbxml_chars_merge(mms, mms_nap);

#if GUTIL_LOG_DEBUG
		if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
			GHashTableIter iter;
			gpointer key, value;
			LOG("MMS:")
			g_hash_table_iter_init(&iter, mms);
			while (g_hash_table_iter_next(&iter, &key, &value)) {
				LOG("  %s = %s", (char*)key, (char*)value);
			}
		}
#endif

		/* APN is required */
		apn = g_hash_table_lookup(mms, PARM_NAP_ADDRESS);
		addr_type = g_hash_table_lookup(mms, PARM_NAP_ADDRTYPE);
		if (apn && apn[0] && !g_strcmp0(addr_type, NAP_ADDRTYPE_APN)) {
			data->mms = g_new0(struct provisioning_mms, 1);
			data->mms->apn = g_strdup(apn);
			data->mms->authtype = provisioning_wbxml_chars_authtype(mms);
			data->mms->name =
				g_strdup(g_hash_table_lookup(mms, PARM_NAME));
			data->mms->username =
				g_strdup(g_hash_table_lookup(mms, PARM_AUTHNAME));
			data->mms->password =
				g_strdup(g_hash_table_lookup(mms, PARM_AUTHSECRET));
			data->mms->messagecenter =
				g_strdup(g_hash_table_lookup(mms, PARM_ADDR));
			data->mms->messageproxy =
				g_strdup(g_hash_table_lookup(mms, PARM_PXADDR));
			data->mms->portnro =
				g_strdup(g_hash_table_lookup(mms, PARM_PORTNBR));
		} else {
			GERR("No internet APN");
		}
		g_hash_table_destroy(mms);
	}

	return data;
}

static
struct provisioning_wbxml_context*
provisioning_wbxml_context_new(void)
{
	return g_new0(struct provisioning_wbxml_context, 1);
}

static
void
provisioning_wbxml_context_free(
	struct provisioning_wbxml_context *context)
{
	if (context) {
		g_slist_free_full(context->napdef, provisioning_wbxml_chars_free);
		g_slist_free_full(context->application, provisioning_wbxml_chars_free);
		g_slist_free_full(context->pxlogical, provisioning_wbxml_chars_free);
		g_free(context);
	}
}

struct provisioning_data*
decode_provisioning_wbxml(
	const guint8 *bytes,
	int len)
{
	static WBXMLContentHandler prov_content_handler = {
		NULL,                               /* start_document_clb */
		NULL,                               /* end_document_clb */
		provisioning_wbxml_start_element,   /* start_element_clb */
		provisioning_wbxml_end_element,     /* end_element_clb */
		NULL,                               /* characters_clb */
		NULL                                /* pi_clb */
	};

	struct provisioning_data *result = NULL;
	struct provisioning_wbxml_context *context =
		provisioning_wbxml_context_new();

	WBXMLError err;
	WBXMLParser *parser = wbxml_parser_create();
	wbxml_parser_set_main_table(parser, prov_table);
	wbxml_parser_set_content_handler(parser, &prov_content_handler);
	wbxml_parser_set_user_data(parser, context);
	err = wbxml_parser_parse(parser, (void*)bytes, len);
	if (err == WBXML_OK) {
		LOG("WBXML parsing OK");
		result = provisioning_wbxml_context_data(context);
	} else {
		GERR("WBXML parsing error %d %s", err, wbxml_errors_string(err));
	}
	wbxml_parser_destroy(parser);
	provisioning_wbxml_context_free(context);
	return result;
}

static
void
provisioning_internet_free(
	struct provisioning_internet *internet)
{
	if (internet) {
		g_free(internet->name);
		g_free(internet->apn);
		g_free(internet->username);
		g_free(internet->password);
		g_free(internet);
	}
}

static
void
provisioning_mms_free(
	struct provisioning_mms *mms)
{
	if (mms) {
		g_free(mms->name);
		g_free(mms->apn);
		g_free(mms->username);
		g_free(mms->password);
		g_free(mms->messageproxy);
		g_free(mms->messagecenter);
		g_free(mms->portnro);
		g_free(mms);
	}
}

void
provisioning_data_free(
	struct provisioning_data *data)
{
	if (data) {
		provisioning_internet_free(data->internet);
		provisioning_mms_free(data->mms);
		g_free(data);
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
