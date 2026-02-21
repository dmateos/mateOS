#ifndef MATEOS_DLFCN_H
#define MATEOS_DLFCN_H

#define RTLD_LAZY 1
#define RTLD_NOW 2

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);
char *dlerror(void);

#endif
