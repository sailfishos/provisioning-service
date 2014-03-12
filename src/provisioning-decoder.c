/*
 *  Copyright (C) 2014 Jolla Ltd.
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

gboolean decode_provisioning_wbxml(const char *array, int array_len)
{
	WB_UTINY *wbxml, *xml;
	WB_ULONG wbxml_len, xml_len;
	WBXMLGenXMLParams params;

	params.lang = WBXML_LANG_PROV10;
	params.gen_type = WBXML_GEN_XML_INDENT;
	params.indent = 2;
	
	wbxml = (WB_UTINY*) array;
	wbxml_len = (WB_ULONG) array_len;
	WBXMLError error = wbxml_conv_wbxml2xml_withlen(wbxml, wbxml_len,
							&xml, &xml_len,
							&params);

	if( error == WBXML_OK ) {
#ifdef FILEWRITE
		char *file_name = "wbxml2xml.xml";
		print_to_file((char*)xml,(int) xml_len, file_name);
#endif
		parse_xml_main((char*)xml,(int) xml_len);

		wbxml_free(xml);
		LOG("parsing done");
		return TRUE;
	}

	return FALSE;
}
#ifdef FILEWRITE
void print_to_file(char *array, int array_len, char *file_name)
{
	LOG("array_len:%d", array_len);
	FILE* fd = NULL;
	char *path = FILEWRITE;
	
	LOG("%s",path);
	path = g_strconcat(FILEWRITE "/", file_name, NULL);
	fd = fopen(path, "w+");
	if(!fd) {
		LOG("fopen() Error!!!\n");
		goto out;
	}

	if(array_len != fwrite(array,1,array_len,fd)) {
		LOG("\n fwrite() failed\n");
		goto out;
	}

	LOG("fwrite() successful, data written to text file\n");

	fclose(fd);

out:
	LOG("leaving");
}
#endif
