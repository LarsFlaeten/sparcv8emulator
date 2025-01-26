/* PROM Node Operations interface
 *
 * (C) Copyright 2011 Frontgrade Gaisler
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */


#ifndef __PROM_NO_H__
#define __PROM_NO_H__

struct node;  /* Node */
struct prop;  /* Property */

/* The Root Nodes, defined by the prom.c */
extern struct node *node_root;

struct node {
	struct node *child;
	struct node *sibling;
	struct prop *props;
};

/* Property Header for all standard properties */
struct prop {
	unsigned short options;
	unsigned short length;
	char *name;
};

/* Property Options Bit Declarations (prop.options) */
#define PO_NEXT		0x0000
#define PO_NONEXT	0x8000
#define PO_DATA		0x0000
#define PO_PTR		0x4000
#define PO_END		0x2000 /* End of Array/List */
#define PO_REMOVED	0x1000 /* Property is considered removed */
#define PO_TYPE_MASK	0x00ff /* Custom Node Type Number goes here (N/A) */

struct prop_std {
	unsigned short options;
	unsigned short length;
	char *name;
	struct prop *next;
	union {
		int data[0];
		int *value;
	} v;
};

struct prop_data {
	unsigned short options;
	unsigned short length;
	char *name;
	struct prop *next;
	int data[0];
};

struct prop_ptr {
	unsigned short options;
	unsigned short length;
	char *name;
	struct prop *next;
	int *value;
};

struct propa_data {
	unsigned short options;
	unsigned short length;
	char *name;
	int data[0];
};

struct propa_ptr {
	unsigned short options;
	unsigned short length;
	char *name;
	int *value;
};

#define PROPA_PTR(name, ptr, length) \
	{PO_NONEXT|PO_PTR, length, name, (void *)ptr}
#define PROPA_PTR_END(name, ptr, length) \
	{PO_NONEXT|PO_PTR|PO_END, length, name, (void *)ptr}
#define PROPA_INT(name, i) {PO_NONEXT|PO_DATA, 4, name, (void *)i}
#define PROPA_INT_END(name, i) \
	{PO_NONEXT|PO_DATA|PO_END, 4, name, (void *)i}

/* xml extra macros and data structures */
#define PROPA_RM(name) \
	{PO_NONEXT|PO_PTR|PO_REMOVED, 0, name, NULL}
#define PROPA_RM_END(name) \
	{PO_NONEXT|PO_PTR|PO_REMOVED|PO_END, 0, name, NULL}

struct xml_node {
	struct node node;
	short propcount;
};

#define XMLMATCH_CORE	0x001
#define XMLMATCH_BUS	0x002
#define XMLMATCH_REMOVE	0x100

struct xml_match {
	/* Mathing cores */
	int vendor;
	int device;
	int index;
	int coreindex;

	/* Matching buses */
	int bus;

	/* General fields */
	int flags;
	int placeholder;
	int chtarget_start;
	int chtarget_count;
};

/* PROM Node Operations Called from Linux */
extern int no_nextnode(int node);
extern int no_child(int node);
extern int no_proplen(int node, const char *name);
extern int no_getprop(int node, const char *name, char *val);
extern int no_setprop(int node, const char *name, char *val, int len);
extern char *no_nextprop(int node, char *name);

/* PROM Edit Device Structures (prior to booting Linux) */
extern void prop_add(struct node *n, struct prop *newprop);
extern void prop_del(struct node *n, char *name);
extern void prop_add_dup(struct node *n, struct prop_std *buf,
			  int size, struct prop_std **lastprop);

#endif
