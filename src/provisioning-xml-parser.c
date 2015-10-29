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

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include "log.h"
#include "provisioning-xml-parser.h"

struct provisioning_data *prov_data;

struct provisioning_internet *prov_data_internet(struct provisioning_data *data)
{
	if (!data->internet) {
		data->internet = g_new0(struct provisioning_internet, 1);
	}
	return data->internet;
}

struct provisioning_mms *prov_data_mms(struct provisioning_data *data)
{
	if (!data->mms) {
		data->mms = g_new0(struct provisioning_mms, 1);
	}
	return data->mms;
}

static xmlDocPtr parse_xml_to_tree(const char *content, int length)
{
	xmlDocPtr doc; /* the resulting document tree */

	/*
	 * The document being in memory, it have no base per RFC 2396,
	 * and the "noname.xml" argument will serve as its base.
	 */
	doc = xmlReadMemory(content, length, "noname.xml", NULL, XML_PARSE_NOBLANKS);
	if (doc == NULL) {
		LOG("Failed to parse document");
		return NULL;
    }
	return doc;
}

static xmlChar *get_characteristic_type(xmlNodePtr cur)
{
	xmlChar *value;
	value = NULL;
	if (cur == NULL)
		return value;
	if ((!xmlStrcmp(cur->name, (const xmlChar *)"characteristic"))){
		value = xmlGetProp(cur, (const xmlChar *)"type");
	}
	return value;
}

static xmlChar *parse_attribute(xmlNodePtr cur, const xmlChar *parm_name)
{
	xmlChar *value,*name;

	value = NULL;
	name = NULL;
	if (cur == NULL)
		return value;
	cur = cur->xmlChildrenNode;

	while (cur != NULL) {
		if (xmlStrcmp(cur->name, (const xmlChar *)"parm")) {
			cur = cur->next;
			continue;
		}

		name = xmlGetProp(cur, (const xmlChar *)"name");
		if (xmlStrcmp(name,parm_name)) {
			xmlFree(name);
			cur = cur->next;
			continue;
		}

		value = xmlGetProp(cur, (const xmlChar *)"value");
		xmlFree(name);
		break;

	}
	return value;
}

static gboolean parm_exists(xmlNodePtr cur, const xmlChar *parm_name)
{
	xmlChar * name;
	gboolean ret;

	ret = FALSE;

	if (cur == NULL)
		return ret;

	cur = cur->xmlChildrenNode;

	while (cur != NULL) {
		if (xmlStrcmp(cur->name, (const xmlChar *)"parm")) {
			cur = cur->next;
			continue;
		}

		name = xmlGetProp(cur, (const xmlChar *)"name");
		if (xmlStrcmp(name,parm_name)) {
			xmlFree(name);
			cur = cur->next;
			continue;
		}

		ret = TRUE;
		xmlFree(name);
		break;
	}
	return ret;
}

static xmlChar *get_name(xmlNodePtr cur)
{
	return parse_attribute(cur,(const xmlChar *)"NAME");
}

static xmlChar *get_apn(xmlNodePtr cur)
{
	xmlChar *value;
	value = parse_attribute(cur,(const xmlChar *)"NAP-ADDRTYPE");
	if (!xmlStrcmp(value, (const xmlChar *)"APN")) {
		xmlFree(value);
		return parse_attribute(cur,(const xmlChar *)"NAP-ADDRESS");
	} else {
		xmlFree(value);
		return NULL;
	}
}

static xmlChar *get_addr(xmlNodePtr cur)
{
	return parse_attribute(cur,(const xmlChar *)"ADDR");
}

static xmlChar *get_to_proxy(xmlNodePtr cur)
{
	return parse_attribute(cur,(const xmlChar *)"TO-PROXY");
}

static void handle_napauthinfo(xmlNodePtr temp, xmlChar *ctype, char *app_id)
{
	xmlChar *attr;
	char *username, *password;

	username = password = NULL;

	if (xmlStrcmp(ctype,(const xmlChar *)"NAPAUTHINFO"))
		return;

	attr = parse_attribute(temp,(const xmlChar *)"AUTHNAME");
	if (attr != NULL) {
		username = g_strdup((char *)attr);
		xmlFree(attr);
	}

	attr = parse_attribute(temp, (const xmlChar *)"AUTHSECRET");
	if (attr != NULL) {
		password = g_strdup((char *)attr);
		xmlFree(attr);
	}

	if (g_strcmp0(app_id,"w4") == 0) {
		g_free(prov_data_mms(prov_data)->username);
		prov_data_mms(prov_data)->username = username;
	} else if (g_strcmp0(app_id,"w2") == 0 ||
	           g_strcmp0(app_id,"internet") == 0) {
		g_free(prov_data_internet(prov_data)->username);
		prov_data_internet(prov_data)->username = username;
	} else
		g_free(username);

	if (g_strcmp0(app_id,"w4") == 0) {
		g_free(prov_data_mms(prov_data)->password);
		prov_data_mms(prov_data)->password = password;
	} else if (g_strcmp0(app_id,"w2") == 0 ||
	           g_strcmp0(app_id,"internet") == 0) {
		g_free(prov_data_internet(prov_data)->password);
		prov_data_internet(prov_data)->password = password;
	} else
		g_free(password);
}

/*
 * Get internet access point data
 * Rudimentary solution.
 * Properly done this would parse all the attributes to a table and then decide
 * what to do.
 */
static gboolean parse_internet(xmlNodePtr cur)
{
	xmlChar *name,*ctype, *ctype_child;
	xmlNodePtr temp, temp1;
	gboolean ret;

	name = NULL;
	temp = cur;
	ret = FALSE;
	LOG("parse_internet");
	/*
	 * Let's check first if "INTERNET" exists and if so let's use that and
	 * forget the rest.
	 */
	while (temp != NULL) {
		/*
		 * This needs to be done this way as get_characteristic_type
		 * uses xmlGetProp and that states the return value must be
		 * freed by caller with xmlFree(). For further information
		 * about libxml2 see http://xmlsoft.org/html/
		 */
		ctype = get_characteristic_type(temp);
		if (xmlStrcmp(ctype,(const xmlChar *)"NAPDEF")) {
			xmlFree(ctype);
			temp = temp->next;
			continue;
		}
		if (!parm_exists(temp, (const xmlChar *)"INTERNET")) {
			xmlFree(ctype);
			temp = temp->next;
			continue;
		}

		name = get_name(temp);
		if (name != NULL) {
			g_free(prov_data_internet(prov_data)->name);
			prov_data_internet(prov_data)->name = g_strdup((char*)name);
			xmlFree(name);
		}

		if (name != NULL) {
			name = get_apn(temp);
			g_free(prov_data_internet(prov_data)->apn);
			prov_data_internet(prov_data)->apn = g_strdup((char *)name);
			xmlFree(name);
		}

		temp1 = temp->xmlChildrenNode;
		while (temp1 != NULL) {

			ctype_child = get_characteristic_type(temp1);

			handle_napauthinfo(temp1, ctype_child,"internet");

			xmlFree(ctype_child);
			temp1 = temp1->next;
		}

		xmlFree(ctype);
		ret = TRUE;
		break;
	}
	return ret;
}

static void handle_application(xmlNodePtr temp, xmlChar *ctype,
					char *ptr_to_id, char *app_id)
{
	xmlChar *attr, *name;

	if (xmlStrcmp(ctype,(const xmlChar *)"APPLICATION"))
		return;

	attr = parse_attribute(temp,(const xmlChar *)"APPID");

	/*w2*/
	if ((!xmlStrcmp(attr, (const xmlChar *)"w2")) &&
		(!xmlStrcmp(attr, (const xmlChar *)app_id))) {
		name = parse_attribute(temp, (const xmlChar *)"TO-NAPID");
		strcpy(ptr_to_id, (char *)name);
		xmlFree(name);
	}

	/*w4*/
	if ((!xmlStrcmp(attr,(const xmlChar *)"w4")) &&
		(!xmlStrcmp(attr,(const xmlChar *)app_id))) {

		name = get_addr(temp);
		if (name != NULL) {
			g_free(prov_data_mms(prov_data)->messagecenter);
			prov_data_mms(prov_data)->messagecenter = g_strdup((char*)name);
			xmlFree(name);
		}

		name = get_to_proxy(temp);
		strcpy(ptr_to_id, (char *)name);
		xmlFree(name);
	}
	xmlFree(attr);
}

static void handle_napdef(xmlNodePtr temp, xmlChar *ctype,
					char *ptr_to_nap_id, char *app_id)
{
	xmlChar *attr, *child_attr, *ctype_child;
	xmlNodePtr temp1;
	char *apn, *name;

	apn = name = NULL;

	if (xmlStrcmp(ctype,(const xmlChar *)"NAPDEF")) {
		return;
	}

	attr = parse_attribute(temp,(const xmlChar *)"NAPID");
	if (xmlStrcmp(attr,(const xmlChar *)ptr_to_nap_id)) {
		xmlFree(attr);
		return;
	}
	xmlFree(attr);

	child_attr = get_apn(temp);
	if (child_attr != NULL) {
		apn = g_strdup((char *)child_attr);
		xmlFree(child_attr);
	}

	child_attr = get_name(temp);
	if (child_attr != NULL) {
		name = g_strdup((char*)child_attr);
		xmlFree(child_attr);
	}

	temp1 = temp->xmlChildrenNode;
	while (temp1 != NULL) {

		ctype_child = get_characteristic_type(temp1);
		handle_napauthinfo(temp1, ctype_child, app_id);

		xmlFree(ctype_child);
		temp1 = temp1->next;
	}

	if (g_strcmp0(app_id,"w4") == 0) {
		g_free(prov_data_mms(prov_data)->apn);
		prov_data_mms(prov_data)->apn = apn;
	} else if (g_strcmp0(app_id,"w2") == 0) {
		g_free(prov_data_internet(prov_data)->apn);
		prov_data_internet(prov_data)->apn = apn;
	} else
		g_free(apn);

	if (g_strcmp0(app_id,"w4") == 0) {
		g_free(prov_data_mms(prov_data)->name);
		prov_data_mms(prov_data)->name = name;
	} else if (g_strcmp0(app_id,"w2") == 0) {
		g_free(prov_data_internet(prov_data)->name);
		prov_data_internet(prov_data)->name = name;
	} else
		g_free(name);
}

/*
 * Get w2 data
 * Rudimentary solution.
 * Properly done this would parse all the attributes to a list of w2 structures
 * and then decide what to do.
 */
static void parse_w2(xmlNodePtr cur)
{
	xmlChar *ctype;
	xmlNodePtr temp;
	/*as stated in Provisioning Content 1.1*/
	char to_nap_id[16]= ""; /* TODO: make dynamic*/
	char *ptr_to_nap_id;

	ptr_to_nap_id = to_nap_id;
	LOG("parse_w2");

	temp = cur;
	while (temp != NULL) {

		ctype = get_characteristic_type(temp);

		handle_application(temp, ctype, ptr_to_nap_id, "w2");

		xmlFree(ctype);
		temp = temp->next;
	}


	temp = cur;
	/*get matching apn*/
	while (temp != NULL) {

		ctype = get_characteristic_type(temp);

		handle_napdef(temp, ctype, ptr_to_nap_id, "w2");

		xmlFree(ctype);
		temp = temp->next;
	}

}

static void handle_w4_port(xmlNodePtr temp, xmlChar *ctype)
{
	xmlChar *attr;

	if (xmlStrcmp(ctype,(const xmlChar *)"PORT"))
		return;

	attr = parse_attribute(temp,(const xmlChar *)"PORTNBR");
	if (attr != NULL) {
		g_free(prov_data_mms(prov_data)->portnro);
		prov_data_mms(prov_data)->portnro = g_strdup((char *)attr);
		xmlFree(attr);
	}
}

static void handle_w4_pxphysical(xmlNodePtr temp, xmlChar *ctype,
							char *ptr_to_nap_id)
{

	xmlNodePtr temp1;
	xmlChar *ctype_child, *attr;

	if (xmlStrcmp(ctype,(const xmlChar *)"PXPHYSICAL"))
		return;

	/*get proxy*/
	attr = parse_attribute(temp,(const xmlChar *)"PXADDR");
	if (attr != NULL) {
		g_free(prov_data_mms(prov_data)->messageproxy);
		prov_data_mms(prov_data)->messageproxy = g_strdup((char*)attr);
		xmlFree(attr);
	}

	attr = parse_attribute(temp,(const xmlChar *)"TO-NAPID");
	strcpy(ptr_to_nap_id, (char *)attr);
	xmlFree(attr);

	temp1 = temp->xmlChildrenNode;
	/*get portnbr*/
	while (temp1 != NULL) {
		ctype_child = get_characteristic_type(temp1);
		handle_w4_port(temp1, ctype_child);
		xmlFree(ctype_child);
		temp1 = temp1->next;
	}
}

static void handle_w4_pxlogical(xmlNodePtr temp, xmlChar *ctype,
					char *ptr_to_proxy, char *ptr_to_nap_id)
{
	xmlChar *attr, *ctype_child;
	xmlNodePtr temp1;

	if (xmlStrcmp(ctype,(const xmlChar *)"PXLOGICAL"))
		return;

	attr = parse_attribute(temp,(const xmlChar *)"PROXY-ID");
	if (xmlStrcmp(attr,(const xmlChar *)ptr_to_proxy)) {
		xmlFree(attr);
		return;
	}

	xmlFree(attr);
	LOG("handle_w4_pxlogical");
	temp1 = temp->xmlChildrenNode;
	while (temp1 != NULL) {
		ctype_child = get_characteristic_type(temp1);
		handle_w4_pxphysical(temp1, ctype_child, ptr_to_nap_id);
		xmlFree(ctype_child);
		temp1 = temp1->next;
	}
}

/*
 * Get w4 aka mms data
 * Rudimentary solution.
 * Properly done this would parse all the attributes to a list of w4 structures
 * and then decide what to do.
 */
static void parse_w4(xmlNodePtr cur)
{
	xmlChar *ctype;
	/*
	 * As stated in Provisioning Content 1.1
	 * NOTE! if ipv6 then should be 45
	 */
	char to_proxy[32] = ""; //make dynamic
	/* As stated in Provisioning Content 1.1*/
	char to_nap_id[16]= ""; //make dynamic
	char *ptr_to_proxy,*ptr_to_nap_id;
	xmlNodePtr temp;

	temp = cur;

	LOG("parse_w4");
	ptr_to_proxy = to_proxy;
	ptr_to_nap_id = to_nap_id;
	temp = cur;

	while (temp != NULL) {

		ctype = get_characteristic_type(temp);

		handle_application(temp, ctype, ptr_to_proxy, "w4");

		xmlFree(ctype);
		temp = temp->next;
	}

	temp = cur;

	/*get matching proxy*/
	while (temp != NULL) {

		ctype = get_characteristic_type(temp);

		handle_w4_pxlogical(temp, ctype, ptr_to_proxy, ptr_to_nap_id);

		xmlFree(ctype);
		temp = temp->next;
	}

	temp = cur;
	/*get matching apn*/
	while (temp != NULL) {

		ctype = get_characteristic_type(temp);

		handle_napdef(temp, ctype, ptr_to_nap_id, "w4");

		xmlFree(ctype);
		temp = temp->next;
	}
}

static void provisioning_internet_free(struct provisioning_internet *internet)
{
    if (internet) {
		g_free(internet->name);
		g_free(internet->apn);
		g_free(internet->username);
		g_free(internet->password);
        g_free(internet);
    }
}

static void provisioning_mms_free(struct provisioning_mms *mms)
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

void provisioning_data_free(struct provisioning_data *data)
{
	LOG("clean_provisioning_data");
	provisioning_internet_free(data->internet);
	provisioning_mms_free(data->mms);
	g_free(data);
}

struct provisioning_data *parse_xml_main(const char *document,int length)
{
	xmlDocPtr doc;
	xmlNodePtr cur;

	LOG("enter parse_xml_main");
	prov_data = g_new0(struct provisioning_data, 1);

	doc = parse_xml_to_tree(document, length);

	cur = xmlDocGetRootElement(doc);
	if (cur == NULL) {
		LOG("empty document");
		xmlFreeDoc(doc);
		provisioning_data_free(prov_data);
		return NULL;
	}

	/*check document type*/
	if (xmlStrcmp(cur->name, (const xmlChar *) "wap-provisioningdoc")) {
		LOG("root node != wap-provisioningdoc");
		xmlFreeDoc(doc);
		provisioning_data_free(prov_data);
		return NULL;
	}

	cur = cur->xmlChildrenNode;

	/*
	 * Let's check first if "INTERNET" exists and if so let's use that and
	 * forget the rest.
	 */
	if (!parse_internet(cur)) {
		parse_w2(cur);
	}

	/*parse mms*/
	parse_w4(cur);
	xmlFreeDoc(doc);
	xmlCleanupParser();

	LOG("exit parse_xml_main");
	return prov_data;
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
