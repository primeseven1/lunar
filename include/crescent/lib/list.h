#pragma once

#include <crescent/common.h>

#define list_node_init(node, prev, next) \
	do { \
		(node)->prev = NULL; \
		(node)->next = NULL; \
	} while (0)

#define list_insert_head(head, node, prev, next) \
	do { \
		__typeof__(head)* __h   = &(head); \
		__typeof__(*(__h)) __old = *(__h); \
		(node)->next = __old; \
		(node)->prev = NULL; \
		if (__old) \
			__old->prev = (node); \
		*(__h) = (node); \
	} while (0)

#define list_insert_after(pos, node, prev, next) \
	do { \
		__typeof__(pos) __p = (pos); \
		(node)->next = __p->next; \
		(node)->prev = __p; \
		if (__p->next) \
			__p->next->prev = (node); \
		__p->next = (node); \
	} while (0)

#define list_remove(head, node, prev, next) \
	do { \
		__typeof__(head)* __h  = &(head); \
		__typeof__(node) __n   = (node); \
		if (__n->prev) \
			__n->prev->next = __n->next; \
		else \
			*(__h) = __n->next; \
		if (__n->next) \
			__n->next->prev = __n->prev; \
		__n->prev = __n->next = NULL; \
	} while (0)
