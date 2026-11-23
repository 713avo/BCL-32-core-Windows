/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Event System API
 */

#ifndef BCL_EVTSYS_H
#define BCL_EVTSYS_H

#include "bcl.h"

#define BCL_HANDLER_BUCKETS 128

struct bcl_handler {
    char *source;
    char *proc_name;
    struct bcl_handler *next;
    struct bcl_handler *hash_next;
};

bcl_evalstatus_t bcl_fire_callback(bcl_interp_t *interp, const char *source, const char *proc_name, bcl_val_t data);
void bcl_evtsys_register(bcl_interp_t *interp, const char *source, const char *proc_name);
int bcl_evtsys_unregister(bcl_interp_t *interp, const char *source);
const char *bcl_evtsys_lookup(bcl_interp_t *interp, const char *source);
int bcl_evtsys_pending(bcl_interp_t *interp);
void bcl_evtsys_clear(bcl_interp_t *interp);

#endif /* BCL_EVTSYS_H */
