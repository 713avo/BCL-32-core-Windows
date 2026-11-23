/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * linux.bll module integration API
 */

#ifndef BCL_LINUX_EVTMOD_H
#define BCL_LINUX_EVTMOD_H

#include "bcl.h"

#define LINUX_EVTMOD_ABI_VERSION         0x001000
#define LINUX_EVTMOD_ABI_VERSION_V0_8    0x000800
#define LINUX_EVTMOD_ABI_VERSION_V0_8_1  0x000801
#define LINUX_EVTMOD_ABI_VERSION_V0_9    0x000900
#define LINUX_EVTMOD_ABI_VERSION_V1_0    0x001000

#define LINUX_EVTMOD_ABI_MAJOR(v)  (((v) >> 16) & 0xFF)
#define LINUX_EVTMOD_ABI_MINOR(v)  (((v) >>  8) & 0xFF)
#define LINUX_EVTMOD_ABI_PATCH(v)   ((v)        & 0xFF)

typedef struct linux_evtmod_timer linux_evtmod_timer_t;
typedef struct linux_evtmod_loop_ref linux_evtmod_loop_ref_t;

typedef enum {
    LINUX_EVTMOD_OK = 0,
    LINUX_EVTMOD_REMOVE = 1,
    LINUX_EVTMOD_ERROR = 2
} linux_evtmod_result_t;

typedef enum {
    LINUX_EVTMOD_CLOSE_SCRIPT = 0,
    LINUX_EVTMOD_CLOSE_MODULE = 1
} linux_evtmod_close_reason_t;

typedef linux_evtmod_result_t (*linux_evtmod_fd_cb_t)(bcl_interp_t *interp, int fd, void *userdata);
typedef void (*linux_evtmod_timer_cb_t)(bcl_interp_t *interp, void *userdata);
typedef void (*linux_evtmod_close_fn_t)(bcl_interp_t *interp, const char *handle, int fd, void *module_data,
                                        linux_evtmod_close_reason_t reason);

typedef struct linux_evtmod_loop_s {
    void *backend_data;

    int (*fd_register)(void *backend_data, int fd, linux_evtmod_fd_cb_t cb, void *userdata);
    void (*fd_unregister)(void *backend_data, int fd);

    linux_evtmod_timer_t *(*timer_register)(void *backend_data, int ms, int repeat, linux_evtmod_timer_cb_t cb,
                                            void *userdata);
    void (*timer_cancel)(void *backend_data, linux_evtmod_timer_t *timer);

    int (*loop_run)(void *backend_data);
    void (*loop_quit)(void *backend_data);
} linux_evtmod_loop_t;

typedef struct {
    unsigned int abi_version;
    unsigned int struct_size;

    int (*fd_watch_add)(bcl_interp_t *interp, int fd, linux_evtmod_fd_cb_t callback, void *userdata);
    void (*fd_watch_remove)(bcl_interp_t *interp, int fd);

    linux_evtmod_timer_t *(*timer_add)(bcl_interp_t *interp, int ms, int repeat, linux_evtmod_timer_cb_t callback,
                                       void *userdata);
    void (*timer_cancel)(bcl_interp_t *interp, linux_evtmod_timer_t *handle);

    const char *(*handle_create)(bcl_interp_t *interp, int fd, void *module_data, linux_evtmod_close_fn_t close_fn);
    void *(*handle_get_data)(bcl_interp_t *interp, const char *handle);
    int (*handle_get_fd)(bcl_interp_t *interp, const char *handle);
    int (*handle_destroy)(bcl_interp_t *interp, const char *handle);

    int (*loop_install)(bcl_interp_t *interp, linux_evtmod_loop_t *backend);
    void (*loop_uninstall)(bcl_interp_t *interp);

    linux_evtmod_loop_ref_t *(*loop_ref_add)(bcl_interp_t *interp, const char *owner, const char *reason);
    void (*loop_ref_remove)(bcl_interp_t *interp, linux_evtmod_loop_ref_t *ref);
    int (*loop_ref_count)(bcl_interp_t *interp);
} linux_evtmod_api_t;

linux_evtmod_api_t *linux_evtmod_api(bcl_interp_t *interp);

#endif
