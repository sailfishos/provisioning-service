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

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include "log.h"
#include "provisioning-xml-parser.h"

struct provisioning_data *prov_data;

void *get_provisioning_data()
{
	return prov_data;
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
		return parse_attribute(cur,(const xmlChar *)"NAP-ADDRESS");
		xmlFree(value);
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
		username = g_try_new0(char, strlen((char *)attr) + 1);
		strcpy(username,(char *)attr);
		xmlFree(attr);
	}

	attr = parse_attribute(temp, (const xmlChar *)"AUTHSECRET");
	if (attr != NULL) {
		password = g_try_new0(char, strlen((char *)attr) + 1);
		strcpy(password,(char *)attr);
		xmlFree(attr);
	}

	if (g_strcmp0(app_id,"w4") == 0)
		prov_data->w4->username = username;
	else if (g_strcmp0(app_id,"w2") == 0)
		prov_data->w2->username = username;
	else if (g_strcmp0(app_id,"internet") == 0)
		prov_data->internet->username = username;
	else
		g_free(username);

	if (g_strcmp0(app_id,"w4") == 0)
		prov_data->w4->password = password;
	else if (g_strcmp0(app_id,"w2") == 0)
		prov_data->w2->password = password;
	else if (g_strcmp0(app_id,"internet") == 0)
		prov_data->internet->password = password;
	else
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
			prov_data->internet->name = g_try_new0(char,
						strlen((char *)name) + 1);
			strcpy(prov_data->internet->name, (char*)name);
			xmlFree(name);
		}

		if (name != NULL) {
			name = get_apn(temp);
			prov_data->internet->apn = g_try_new0(char,
							strlen((char *)name)+1);
			strcpy(prov_data->internet->apn, (char *)name);
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
		name = parse_attribute(temp,
					(const xmlChar *)"TO-NAPID");
		strcpy(ptr_to_id, (char *)name);
		xmlFree(name);
	}

	/*w4*/
	if ((!xmlStrcmp(attr,(const xmlChar *)"w4")) &&
		(!xmlStrcmp(attr,(const xmlChar *)app_id))) {

		name = get_addr(temp);
		if (name != NULL) {
			prov_data->w4->messagecenter =
				g_try_new0(char, strlen((char *)name) + 1);
			strcpy(prov_data->w4->messagecenter, (char*)name);
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
		apn = g_try_new0(char, strlen((char *)child_attr) + 1);
		strcpy(apn, (char*)child_attr);
		xmlFree(child_attr);
	}

	child_attr = get_name(temp);
	if (child_attr != NULL) {
		name = g_try_new0(char, strlen((char *)child_attr) + 1);
		strcpy(name, (char*)child_attr);
		xmlFree(child_attr);
	}

	temp1 = temp->xmlChildrenNode;
	while (temp1 != NULL) {

		ctype_child = get_characteristic_type(temp1);
		handle_napauthinfo(temp1, ctype_child, app_id);

		xmlFree(ctype_child);
		temp1 = temp1->next;
	}

	if (g_strcmp0(app_id,"w4") == 0)
		prov_data->w4->apn = apn;
	else if (g_strcmp0(app_id,"w2") == 0)
		prov_data->w2->apn = apn;
	else
		g_free(apn);

	if (g_strcmp0(app_id,"w4") == 0)
		prov_data->w4->name = name;
	else if (g_strcmp0(app_id,"w2") == 0)
		prov_data->w2->name = name;
	else
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
		prov_data->w4->portnro =
				g_try_new0(char, strlen((char *)attr) +1);
		strcpy(prov_data->w4->portnro, (char*)attr);
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
		prov_data->w4->messageproxy = g_try_new0(char,
						strlen((char *)attr) + 1);
		strcpy(prov_data->w4->messageproxy, (char*)attr);
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

gboolean init_w2_data()
{
	struct w2 *net;

	net = g_try_new0(struct w2, 1);
	if (net == NULL)
		return FALSE;//-ENOMEM;

	net->apn = NULL;
	net->name = NULL;
	net->password = NULL;
	net->username = NULL;
	prov_data->w2 = net;

	return TRUE;
}

gboolean init_w4_data()
{
	struct w4 *mms;

	mms = g_try_new0(struct w4, 1);
	if (mms == NULL)
		return FALSE;//-ENOMEM;

	mms->apn = NULL;
	mms->name = NULL;

	mms->username = NULL;
	mms->password = NULL;
	mms->messageproxy = NULL;
	mms->messagecenter = NULL;

	prov_data->w4 = mms;

	return TRUE;
}

gboolean init_internet_data()
{
	struct internet *net;

	net = g_try_new0(struct internet, 1);
	if (net == NULL)
		return FALSE;//-ENOMEM;

	net->apn = NULL;
	net->name = NULL;
	net->username = NULL;
	net->password = NULL;
	prov_data->internet = net;

	return TRUE;
}

gboolean init_provisioning_data()
{

	prov_data = g_try_new0(struct provisioning_data, 1);
	if (prov_data == NULL)
		return FALSE;//-ENOMEM;

	prov_data->internet = NULL;
	prov_data->w2 = NULL;
	prov_data->w4 = NULL;

	if (!init_internet_data())
		return FALSE;
	/*
	 * TODO: This should probably be dynamic i.e. create a list here and
	 * call init when needed and same goes with w2
	 */
	if (!init_w4_data())
		return FALSE;

	if (!init_w2_data())
		return FALSE;

	return TRUE;
}

void clean_provisioning_data()
{
	LOG("clean_provisioning_data");
	g_free(prov_data->internet->apn);
	g_free(prov_data->internet->name);
	g_free(prov_data->internet->username);
	g_free(prov_data->internet->password);
	g_free(prov_data->internet);
	/*
	 * As an example:
	 * g_slist_free_full(prov_data->w2,g_free);
	 * g_slist_free_full(prov_data->w4,g_free);
	 */
	g_free(prov_data->w2->apn);
	g_free(prov_data->w2->name);
	g_free(prov_data->w2->username);
	g_free(prov_data->w2->password);
	g_free(prov_data->w2);
	g_free(prov_data->w4->apn);
	g_free(prov_data->w4->name);
	g_free(prov_data->w4->username);
	g_free(prov_data->w4->password);
	g_free(prov_data->w4->messageproxy);
	g_free(prov_data->w4->messagecenter);
	g_free(prov_data->w4->portnro);
	g_free(prov_data->w4);
	g_free(prov_data);
}

int parse_xml_main(const char *document,int length)
{
	xmlDocPtr doc;
	xmlNodePtr cur;

	LOG("enter parse_xml_main");
	prov_data = NULL;

	if(!init_provisioning_data())
		return 0;

	doc = parse_xml_to_tree(document, length);

	cur = xmlDocGetRootElement(doc);
	if (cur == NULL) {
		LOG("empty document");
		xmlFreeDoc(doc);
		return 0;
	}

	/*check document type*/
	if (xmlStrcmp(cur->name, (const xmlChar *) "wap-provisioningdoc")) {
		LOG("root node != wap-provisioningdoc");
		xmlFreeDoc(doc);
		return 0;
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
	return 1;
}
