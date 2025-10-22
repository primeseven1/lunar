#pragma once

#include <lunar/core/vfs.h>
#include <lunar/lib/hashtable.h>

struct tmpfs_node {
	struct vnode vnode;
	struct tmpfs_node* parent;
	char* name;
	u8* data;
	size_t size;
	struct hashtable* children;
};

extern const struct vnode_ops tmpfs_node_ops;
extern const struct filesystem_type tmpfs_type;
