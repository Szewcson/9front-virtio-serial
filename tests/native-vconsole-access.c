/* SPDX-License-Identifier: MIT */
/* Explicit live test only: changes THIS process to none, not the parent/eve.
 * Requires an already published #Y0 endpoint. Never opens data successfully. */
#include <u.h>
#include <libc.h>

static void
denied(char *path, int mode)
{
	char message[ERRMAX];
	int fd;
	fd = open(path, mode);
	if(fd >= 0){ close(fd); sysfatal("none unexpectedly opened %s", path); }
	rerrstr(message, sizeof message);
	if(strstr(message, "permission denied") == nil)
		sysfatal("%s failed for a different reason: %s", path, message);
}

void
main(int argc, char**)
{
	char user[32];
	int fd, n;
	if(argc != 1) sysfatal("usage: qga9-native-vconsole-access");
	if(rfork(RFNOTEG|RFENVG|RFNAMEG|RFFDG) < 0) sysfatal("private groups: %r");
	fd = open("#Y0/status", OREAD);
	if(fd < 0) sysfatal("live endpoint precondition: %r");
	close(fd);
	fd = open("#c/user", OWRITE);
	if(fd < 0) sysfatal("open user device: %r");
	if(write(fd, "none", 4) != 4){ close(fd); sysfatal("drop THIS process to none: %r"); }
	close(fd);
	fd = open("#c/user", OREAD);
	if(fd < 0) sysfatal("read current user: %r");
	n = read(fd, user, sizeof user-1); close(fd);
	if(n < 0) sysfatal("read current user: %r");
	user[n] = 0;
	if(strcmp(user, "none") != 0) sysfatal("privilege-drop precondition not met");
	denied("#Y0/status", OREAD);
	denied("#Y0/data", ORDWR);
	print("live vconsole access: none denied status and data; parent unchanged\n");
	exits(nil);
}
