#include "haiku_integration.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <FindDirectory.h>
#include <OS.h>
#include <Path.h>
#include <String.h>
#include <Url.h>


bool
GxHaikuOpenUrl(const char* url)
{
	BUrl target(url, true);
	status_t status = target.OpenWithPreferredApplication();
	if (status != B_OK)
		fprintf(stderr, "[haiku] cannot open %s: %s\n", url, strerror(status));
	return status == B_OK;
}


static bool
Exists(const char* path)
{
	struct stat st;
	return path != NULL && stat(path, &st) == 0;
}


void
GxHaikuPreloadWaylandServer()
{
	if (getenv("GX_HAIKU_SYSTEM_WAYLAND") != NULL)
		return;

	const char* name = "wayland-server-inproc.so";
	char path[B_PATH_NAME_LENGTH];
	path[0] = '\0';

	// 1. set by hand; 2. next to the program (a build in its folder); 3. lib/guitarix of the package
	const char* forced = getenv("GX_HAIKU_WAYLAND_SERVER");
	if (forced != NULL && Exists(forced))
		snprintf(path, sizeof(path), "%s", forced);

	if (path[0] == '\0') {
		image_info image;
		int32 cookie = 0;
		while (get_next_image_info(0, &cookie, &image) == B_OK) {
			if (image.type == B_APP_IMAGE) {
				BPath dir(image.name);
				if (dir.GetParent(&dir) == B_OK) {
					char candidate[B_PATH_NAME_LENGTH];
					snprintf(candidate, sizeof(candidate), "%s/%s", dir.Path(), name);
					if (Exists(candidate))
						snprintf(path, sizeof(path), "%s", candidate);
				}
				break;
			}
		}
	}

	if (path[0] == '\0') {
		BPath lib;
		if (find_directory(B_SYSTEM_LIB_DIRECTORY, &lib) == B_OK) {
			char candidate[B_PATH_NAME_LENGTH];
			snprintf(candidate, sizeof(candidate), "%s/guitarix/%s", lib.Path(), name);
			if (Exists(candidate))
				snprintf(path, sizeof(path), "%s", candidate);
		}
	}

	if (path[0] == '\0')
		return;

	// loaded under its own path, it is the copy GTK finds when it asks for the library by name
	if (dlopen(path, RTLD_NOW | RTLD_GLOBAL) == NULL)
		fprintf(stderr, "[haiku] cannot load %s: %s\n", path, dlerror());
}
