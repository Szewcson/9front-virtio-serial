/* SPDX-License-Identifier: MIT */
/* Explicit disruptive native fixture, never part of default mk test.
 * One unowned #Y0, no QGA consumer, private host-side test coordinator required.
 * Closing data retires a context. Retains its own /tmp control file as evidence.
 * The coordinator writes byte 1; this process alone publishes phase at byte 0. */
#include <u.h>
#include <libc.h>

static volatile int alarmed;

static void
note(void*, char *message)
{
	if(strcmp(message, "alarm") == 0){
		alarmed = 1;
		noted(NCONT);
	}
	noted(NDFLT);
}

static void
phase(int fd, char value)
{
	if(pwrite(fd, &value, 1, 0) != 1)
		sysfatal("publish fixture phase: %r");
}

static void
waitgo(int fd, char expected)
{
	char value;
	int i;

	for(i = 0; i < 400; i++){
		if(pread(fd, &value, 1, 1) != 1)
			sysfatal("read coordinator byte: %r");
		if(value == expected)
			return;
		if(sleep(50) < 0)
			sysfatal("wait for coordinator: %r");
	}
	sysfatal("coordinator did not supply %c", expected);
}

static void
snapshot(int fd, char *out, int size)
{
	int n;

	n = pread(fd, out, size-1, 0);
	if(n <= 0 || n >= size-1 || out[n-1] != '\n')
		sysfatal("bounded status snapshot: %r");
	out[n] = 0;
}

static void
cancel(int fd, int writing)
{
	char byte, message[ERRMAX];
	long n;

	byte = 0x5a;
	alarmed = 0;
	alarm(500);
	n = writing ? write(fd, &byte, 1) : read(fd, &byte, 1);
	rerrstr(message, sizeof message);
	alarm(0);
	if(n != -1 || !alarmed || strcmp(message, "interrupted") != 0)
		sysfatal("expected interrupted %s, got %ld: %s (alarm %d)",
			writing ? "write" : "read", n, message, alarmed);
}

static void
roundtrip(int fd)
{
	uchar expected[8] = {0, 255, '9', 'f', 'r', 'o', 'n', 't'};
	uchar bytes[8];
	long n;
	int used;

	alarmed = 0;
	alarm(10000);
	for(used = 0; used < sizeof bytes; used += n){
		n = read(fd, bytes+used, sizeof bytes-used);
		if(n <= 0 || n > sizeof bytes-used)
			sysfatal("binary read after cancellation/reconnect: %r");
	}
	if(memcmp(bytes, expected, sizeof bytes) != 0)
		sysfatal("binary fixture mismatch");
	for(used = 0; used < sizeof bytes; used += n){
		n = write(fd, bytes+used, sizeof bytes-used);
		if(n <= 0 || n > sizeof bytes-used)
			sysfatal("binary write after cancellation/reconnect: %r");
	}
	alarm(0);
}

void
main(int argc, char **argv)
{
	char path[80], before[513], after[513], byte, message[ERRMAX], *end;
	Dir *original, *retained;
	int control, data, status, duplicate, other, i, writing;
	long n;

	if(argc != 2 || (strcmp(argv[1], "notes") != 0 &&
	   strcmp(argv[1], "read-fault") != 0 && strcmp(argv[1], "write-fault") != 0))
		sysfatal("usage: qga9-native-vconsole-lifetime notes|read-fault|write-fault");
	if(rfork(RFNOTEG|RFNAMEG|RFENVG|RFFDG) < 0 || notify(note) < 0)
		sysfatal("private fixture groups/notes: %r");
	snprint(path, sizeof path, "/tmp/vconsole-lifetime-%d", getpid());
	control = create(path, ORDWR|OEXCL, 0600);
	if(control < 0 || write(control, "..", 2) != 2)
		sysfatal("exclusive fixture control: %r");
	status = open("#Y0/status", OREAD);
	if(status < 0)
		sysfatal("published endpoint required: %r");
	data = open("#Y0/data", ORDWR);
	if(data < 0)
		sysfatal("unowned endpoint required: %r");
	snapshot(status, before, sizeof before);
	original = dirfstat(status);
	if(original == nil)
		sysfatal("original status Qid: %r");
	other = open("#Y0/data", ORDWR);
	if(other >= 0){
		close(other);
		sysfatal("second data owner unexpectedly opened");
	}
	duplicate = dup(data, -1);
	if(duplicate < 0 || close(duplicate) < 0)
		sysfatal("nonfinal duplicate close: %r");
	if(strcmp(argv[1], "notes") == 0){
		/* Coordinator must leave the backend closed during these operations. */
		for(i = 0; i < 4; i++){
			cancel(data, 0);
			cancel(data, 1);
		}
		phase(control, 'R');
		roundtrip(data);
		phase(control, 'C');
		waitgo(control, 'g'); /* coordinator closed the backend before g */
		phase(control, 'R');
		roundtrip(data);
		print("native lifetime: 8 interrupted operations, exclusive owner, dup and binary reconnect passed\n");
	}else{
		writing = strcmp(argv[1], "write-fault") == 0;
		phase(control, writing ? 'W' : 'R');
		if(writing)
			waitgo(control, 'g'); /* backend confirmed closed before write */
		alarmed = 0;
		alarm(10000);
		byte = 0x5a;
		n = writing ? write(data, &byte, 1) : read(data, &byte, 1);
		rerrstr(message, sizeof message);
		alarm(0);
		if(n != -1 || alarmed || strstr(message, "vconsole") == nil)
			sysfatal("port removal did not wake pending %s: %ld %s (alarm %d)",
				writing ? "write" : "read", n, message, alarmed);
		phase(control, 'H');
		waitgo(control, 'h'); /* deliberately retain the data Chan meanwhile */
		close(data);
		data = -1;
		phase(control, 'D');
		waitgo(control, 't'); /* new endpoint published, old status still open */
		snapshot(status, after, sizeof after);
		retained = dirfstat(status);
		end = strchr(before, '\n');
		if(retained == nil || retained->qid.path != original->qid.path ||
		   end == nil || strncmp(before, after, end-before+1) != 0)
			sysfatal("retained status redirected to replacement");
		print("retained status after replacement:\n%s", after);
		free(retained);
		print("native lifetime: pending %s fault, delayed final clunk and stale status passed\n",
			writing ? "write" : "read");
	}
	if(data >= 0)
		close(data);
	free(original);
	close(status);
	phase(control, 'P');
	close(control);
	exits(nil);
}
