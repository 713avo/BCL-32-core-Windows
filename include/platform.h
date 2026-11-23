/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/*
 * Capa de compatibilidad POSIX / Windows (MinGW-w64).
 *
 * En Linux y macOS solo incluye <dlfcn.h> y define bcl_home_dir(). En Windows
 * traduce la carga dinamica (LoadLibrary/GetProcAddress), realpath y el
 * directorio personal (%USERPROFILE%), y prepara la consola (UTF-8 + VT).
 */
#ifndef BCL_PLATFORM_H
#define BCL_PLATFORM_H

#include <stdlib.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <limits.h>
#  include <stdio.h>

#  define BCL_PATH_SEP "\\"
#  define RTLD_NOW 0
#  define RTLD_GLOBAL 0
#  define RTLD_DEFAULT ((void *)0)

static char bcl_dl_error_buf[512];

static inline void *dlopen(const char *path, int flags) {
    HMODULE h;
    (void)flags;
    h = LoadLibraryA(path);
    if (!h) {
        DWORD code = GetLastError();
        WCHAR wide[400];
        DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, code, 0, wide, (DWORD)(sizeof(wide) / sizeof(wide[0])), NULL);
        bcl_dl_error_buf[0] = '\0';
        /* Mensaje del sistema convertido a UTF-8, sin el \r\n final. */
        while (n > 0 && (wide[n - 1] == L'\r' || wide[n - 1] == L'\n' || wide[n - 1] == L' ')) n--;
        wide[n] = L'\0';
        if (n == 0 || !WideCharToMultiByte(CP_UTF8, 0, wide, -1, bcl_dl_error_buf,
                                           (int)sizeof(bcl_dl_error_buf), NULL, NULL)) {
            snprintf(bcl_dl_error_buf, sizeof(bcl_dl_error_buf), "error %lu", (unsigned long)code);
        }
    }
    return (void *)h;
}

/* RTLD_DEFAULT (NULL) busca en el propio ejecutable. */
static inline void *dlsym(void *handle, const char *name) {
    HMODULE h = handle ? (HMODULE)handle : GetModuleHandleA(NULL);
    return (void *)(uintptr_t)GetProcAddress(h, name);
}

static inline int dlclose(void *handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : 1;
}

/* dladdr: solo se usa dli_fbase (base del modulo que contiene la direccion). */
typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

static inline int dladdr(const void *addr, Dl_info *info) {
    HMODULE h = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)addr, &h)) {
        return 0;
    }
    info->dli_fname = NULL;
    info->dli_fbase = (void *)h;
    info->dli_sname = NULL;
    info->dli_saddr = NULL;
    return 1;
}

static inline const char *dlerror(void) {
    return bcl_dl_error_buf[0] ? bcl_dl_error_buf : NULL;
}

#  ifndef PATH_MAX
#    define PATH_MAX 4096
#  endif
#  define realpath(path, resolved) _fullpath((resolved), (path), PATH_MAX)

/* UTF-8 en la consola y secuencias ANSI (VT) en stdout/stderr. */
static inline void bcl_platform_init(void) {
    DWORD mode;
    HANDLE streams[2];
    int i;

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    streams[0] = GetStdHandle(STD_OUTPUT_HANDLE);
    streams[1] = GetStdHandle(STD_ERROR_HANDLE);
    for (i = 0; i < 2; i++) {
        if (streams[i] != INVALID_HANDLE_VALUE && GetConsoleMode(streams[i], &mode)) {
            SetConsoleMode(streams[i], mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
        }
    }
}

static inline const char *bcl_home_dir(void) {
    const char *home = getenv("USERPROFILE");
    return (home && *home) ? home : getenv("HOME");
}

#else /* POSIX */

#  define BCL_PATH_SEP "/"

#  include <dlfcn.h>

static inline void bcl_platform_init(void) {}

static inline const char *bcl_home_dir(void) {
    return getenv("HOME");
}

#endif

#endif /* BCL_PLATFORM_H */
