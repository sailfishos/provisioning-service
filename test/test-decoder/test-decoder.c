/*
 * Copyright (C) 2015-2017 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "test-common.h"
#include "provisioning-decoder.h"

static TestOpt test_opt;

#define DATA_DIR "../data"
#define TEST_PREFIX "/decoder/"

struct test_decoder_data {
	const char *name;
	const char *file;
	const struct provisioning_data *expected;
};

static
void
test_decoder(
	gconstpointer data)
{
	const struct test_decoder_data *test = data;
	char *path = g_strconcat(DATA_DIR, G_DIR_SEPARATOR_S, test->file, NULL);
	struct provisioning_data *prov;
	gchar *wbxml;
	gsize length;
	LOG("Loading %s", path);
	g_assert(g_file_get_contents(path, &wbxml, &length, NULL));
	prov = decode_provisioning_wbxml((void*)wbxml, length);
	g_assert(prov);

	if (test->expected->internet) {
		const struct provisioning_internet *decoded = prov->internet;
		const struct provisioning_internet *expected = test->expected->internet;
		g_assert(decoded);
		g_assert(!g_strcmp0(decoded->name, expected->name));
		g_assert(!g_strcmp0(decoded->apn, expected->apn));
		g_assert(!g_strcmp0(decoded->username, expected->username));
		g_assert(!g_strcmp0(decoded->password, expected->password));
		g_assert(decoded->authtype == expected->authtype);
	} else {
		g_assert(!prov->internet);
	}

	if (test->expected->mms) {
		const struct provisioning_mms *decoded = prov->mms;
		const struct provisioning_mms *expected = test->expected->mms;
		g_assert(decoded);
		g_assert(!g_strcmp0(decoded->name, expected->name));
		g_assert(!g_strcmp0(decoded->apn, expected->apn));
		g_assert(!g_strcmp0(decoded->username, expected->username));
		g_assert(!g_strcmp0(decoded->password, expected->password));
		g_assert(!g_strcmp0(decoded->messageproxy, expected->messageproxy));
		g_assert(!g_strcmp0(decoded->messagecenter, expected->messagecenter));
		g_assert(!g_strcmp0(decoded->portnro, expected->portnro));
		g_assert(decoded->authtype == expected->authtype);
	} else {
		g_assert(!prov->mms);
	}

	provisioning_data_free(prov);
	g_free(wbxml);
	g_free(path);
}

/* ======== Sonera ======== */

static struct provisioning_internet prov_sonera_internet = {
	.name = "Sonera Internet",
	.apn = "internet",
	.username = "",
	.password = "",
	.authtype = AUTH_PAP
};

static struct provisioning_mms prov_sonera_mms = {
	.name = "Sonera MMS",
	.apn = "wap.sonera.net",
	.username = "",
	.password = "",
	.messageproxy = "195.156.25.33",
	.messagecenter = "http://mms.sonera.fi:8002/",
	.portnro = "80",
	.authtype = AUTH_PAP
};

static const struct provisioning_data prov_sonera = {
	.internet = &prov_sonera_internet,
	.mms = &prov_sonera_mms
};

/* ======== DNA ======== */

static struct provisioning_internet prov_dna_internet = {
	.name = "DNA Internet",
	.apn = "internet",
	.authtype = AUTH_UNKNOWN
};

static struct provisioning_mms prov_dna_mms = {
	.name = "DNA MMS",
	.apn = "mms",
	.messageproxy = "10.1.1.2",
	.messagecenter = "http://mmsc.dna.fi",
	.portnro = "8080",
	.authtype = AUTH_UNKNOWN
};

static const struct provisioning_data prov_dna_1 = {
	.internet = &prov_dna_internet
};

static const struct provisioning_data prov_dna_2 = {
	.mms = &prov_dna_mms
};

/* ======== MOI ======== */

static struct provisioning_internet prov_moi_internet = {
	.name = "MOI Internet",
	.apn = "data.moimobile.fi",
	.authtype = AUTH_UNKNOWN
};

static struct provisioning_mms prov_moi_mms = {
	.name = "MOI MMS",
	.apn = "mms",
	.messageproxy = "10.1.1.2",
	.messagecenter = "http://mmsc.dna.fi",
	.portnro = "8080",
	.authtype = AUTH_UNKNOWN
};

static const struct provisioning_data prov_moi_1 = {
	.internet = &prov_moi_internet
};

static const struct provisioning_data prov_moi_2 = {
	.mms = &prov_moi_mms
};

/* ======== Beeline ======== */

static struct provisioning_mms prov_beeline_mms = {
	.name = "Beeline MMS",
	.apn = "mms.beeline.ru",
	.username = "beeline",
	.password = "beeline",
	.messageproxy = "192.168.094.023",
	.messagecenter = "http://mms/",
	.portnro = "8080",
	.authtype = AUTH_PAP
};

static const struct provisioning_data prov_beeline_2 = {
	.mms = &prov_beeline_mms
};

static const struct test_decoder_data tests [] = {
	{ TEST_PREFIX "sonera", "prov_sonera.wbxml", &prov_sonera },
	{ TEST_PREFIX "dna_1", "prov_dna_1.wbxml", &prov_dna_1 },
	{ TEST_PREFIX "dna_2", "prov_dna_2.wbxml", &prov_dna_2 },
	{ TEST_PREFIX "moi_1", "prov_moi_1.wbxml", &prov_moi_1 },
	{ TEST_PREFIX "moi_2", "prov_moi_2.wbxml", &prov_moi_2 },
	{ TEST_PREFIX "beeline_2", "prov_beeline_2.wbxml", &prov_beeline_2 }
};

int main(int argc, char *argv[])
{
	guint i;
	g_test_init(&argc, &argv, NULL);
	test_init(&test_opt, argc, argv);
	for (i = 0; i < G_N_ELEMENTS(tests); i++) {
		const struct test_decoder_data *test = tests + i;
		g_test_add_data_func(test->name, test, test_decoder);
	}
	return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
