/*
 *  Copyright (C) 2014-2015 Jolla Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <glib.h>
#include <wbxml.h>
#include "provisioning-decoder.h"
#include "provisioning-xml-parser.h"
#include <string.h>
#include "log.h"

struct provisioning_data *decode_provisioning_wbxml(const guint8 *array,
								int array_len)
{
	struct provisioning_data * result = NULL;
	WB_UTINY *wbxml, *xml;
	WB_ULONG wbxml_len, xml_len;
	WBXMLGenXMLParams params;
	WBXMLError error;

	memset(&params, 0, sizeof(params));
	params.lang = WBXML_LANG_PROV10;
	params.gen_type = WBXML_GEN_XML_INDENT;
	params.indent = 2;
	
	wbxml = (WB_UTINY*) array;
	wbxml_len = (WB_ULONG) array_len;
	error = wbxml_conv_wbxml2xml_withlen(wbxml, wbxml_len,
							&xml, &xml_len,
							&params);

	if( error == WBXML_OK ) {
#ifdef FILEWRITE
		print_to_file(xml, xml_len, "wbxml2xml.xml");
#endif
		result = parse_xml_main((char*)xml, xml_len);

		wbxml_free(xml);
		LOG("parsing done");
	}

	return result;
}

#ifdef FILEWRITE
void print_to_file(const void *data, int len, const char *file_name)
{
	GError *error = NULL;
	char *path = g_strconcat(FILEWRITE "/", file_name, NULL);
	if (g_file_set_contents(path, data, len, &error)) {
		LOG("wrote file: %s len: %d", path, len);
	} else {
		LOG("%s: %s", path, error->message);
		g_error_free(error);
	}
	g_free(path);
}
#endif
