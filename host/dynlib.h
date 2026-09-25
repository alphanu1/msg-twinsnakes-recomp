/* Loading the native game module: dlopen on Linux and macOS, LoadLibrary
 * on Windows, behind one set of names so module.c is written once. */
#ifndef MGS_DYNLIB_H
#define MGS_DYNLIB_H

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
static inline void* mgs_dlopen(const char* path)
{
    return (void*)LoadLibraryA(path);
}
static inline void* mgs_dlsym(void* handle, const char* name)
{
    return (void*)GetProcAddress((HMODULE)handle, name);
}
static inline void mgs_dlclose(void* handle) { FreeLibrary((HMODULE)handle); }
static inline const char* mgs_dlerror(void)
{
    static char buf[64];
    snprintf(buf, sizeof buf, "Windows error %lu", (unsigned long)GetLastError());
    return buf;
}
#else
#include <dlfcn.h>
static inline void* mgs_dlopen(const char* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static inline void* mgs_dlsym(void* handle, const char* name) { return dlsym(handle, name); }
static inline void mgs_dlclose(void* handle) { dlclose(handle); }
static inline const char* mgs_dlerror(void) { return dlerror(); }
#endif

#endif
