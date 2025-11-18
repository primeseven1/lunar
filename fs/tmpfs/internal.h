#pragma once

#include <lunar/core/vfs.h>
#include <lunar/lib/hashtable.h>

struct tmpfs_node {
	struct vnode vnode;
	struct vattr attr;
	struct tmpfs_node* parent;
	char* name;
	u8* data;
	struct hashtable* children;
};

extern const struct vnode_ops tmpfs_node_ops;
extern const struct filesystem_type tmpfs_type;
