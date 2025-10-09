#pragma once

typedef int uid_t;
typedef int gid_t;

struct cred {
	uid_t uid;
	gid_t gid;
};
