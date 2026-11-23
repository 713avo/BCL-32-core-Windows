/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Event System API implementation
 */

#include <ctype.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "bcl.h"
#include "cmd.h"
#include "eval.h"
#include "evtsys.h"

static unsigned long handler_hash(const char *source) {
    unsigned long hash = 5381;
    unsigned char ch;

    while ((ch = (unsigned char)*source++) != '\0') {
        hash = ((hash << 5) + hash) ^ (unsigned char)tolower(ch);
    }

    return hash;
}

static bcl_handler_t **handler_table(bcl_interp_t *interp) {
    if (!interp->handler_buckets) {
        interp->handler_buckets = bcl_xcalloc(interp, BCL_HANDLER_BUCKETS, sizeof(bcl_handler_t *));
    }
    return (bcl_handler_t **)interp->handler_buckets;
}

static bcl_handler_t *handler_find(bcl_interp_t *interp, const char *source, bcl_handler_t **prev_out) {
    bcl_handler_t *handler;

    if (prev_out) {
        *prev_out = NULL;
    }

    if (interp->handler_buckets) {
        bcl_handler_t **buckets = (bcl_handler_t **)interp->handler_buckets;
        handler = buckets[handler_hash(source) % BCL_HANDLER_BUCKETS];
        while (handler) {
            if (strcasecmp(handler->source, source) == 0) {
                return handler;
            }
            handler = handler->hash_next;
        }
        return NULL;
    }

    handler = interp->handlers;

    while (handler) {
        if (strcasecmp(handler->source, source) == 0) {
            return handler;
        }
        handler = handler->next;
    }

    return NULL;
}

static bcl_handler_t **handler_list_link(bcl_interp_t *interp, bcl_handler_t *target) {
    bcl_handler_t **link = &interp->handlers;

    while (*link) {
        if (*link == target) {
            return link;
        }
        link = &(*link)->next;
    }

    return NULL;
}

static void handler_unlink_hash(bcl_interp_t *interp, bcl_handler_t *target) {
    bcl_handler_t **buckets;
    bcl_handler_t **link;

    if (!interp->handler_buckets || !target) {
        return;
    }

    buckets = (bcl_handler_t **)interp->handler_buckets;
    link = &buckets[handler_hash(target->source) % BCL_HANDLER_BUCKETS];
    while (*link) {
        if (*link == target) {
            *link = target->hash_next;
            target->hash_next = NULL;
            return;
        }
        link = &(*link)->hash_next;
    }
}

bcl_evalstatus_t bcl_fire_callback(bcl_interp_t *interp, const char *source, const char *proc_name, bcl_val_t data) {
    bcl_try_frame_t frame;
    bcl_exec_frame_t exec_frame;
    bcl_val_t previous_data = interp->current_evtdata;
    bcl_val_t previous_source = interp->current_evtsource;
    bcl_result_t result;

    interp->current_evtdata = data.data ? bcl_val_make(data.data, data.len) : bcl_val_empty();
    interp->current_evtsource = source ? bcl_val_make(source, strlen(source)) : bcl_val_empty();

    frame.prev = interp->try_stack;
    interp->try_stack = &frame;
    bcl_frame_push(interp, &exec_frame, BCL_FRAME_CALLBACK, proc_name, interp->current_source_name, 0, 0);

    if (setjmp(frame.env) == 0) {
        result = bcl_eval_invoke(interp, interp->global_env, proc_name, 0, NULL);
        interp->try_stack = frame.prev;
        bcl_frame_pop(interp, &exec_frame);
    } else {
        interp->try_stack = frame.prev;
        bcl_frame_pop(interp, &exec_frame);
        bcl_val_free(&interp->current_evtdata);
        interp->current_evtdata = previous_data;
        bcl_val_free(&interp->current_evtsource);
        interp->current_evtsource = previous_source;

        if (frame.prev) {
            longjmp(frame.prev->env, 1);
        }
        longjmp(interp->error_jmp, 1);
    }

    bcl_val_free(&interp->current_evtdata);
    interp->current_evtdata = previous_data;
    bcl_val_free(&interp->current_evtsource);
    interp->current_evtsource = previous_source;

    if (result.status == EVAL_EXIT) {
        interp->exit_code = result.exit_code;
        interp->pending_status = EVAL_EXIT;
        interp->pending_exit_code = result.exit_code;
    } else if (result.status != EVAL_OK) {
        bcl_val_free(&result.value);
        bcl_raise_code(interp, "EVT", 'F', "BADFLOW", "Invalid control flow in callback: %s", proc_name);
    }

    bcl_val_free(&result.value);
    return result.status;
}

void bcl_evtsys_register(bcl_interp_t *interp, const char *source, const char *proc_name) {
    bcl_handler_t *handler = handler_find(interp, source, NULL);
    bcl_handler_t **buckets;
    unsigned long bucket;

    if (handler) {
        free(handler->proc_name);
        handler->proc_name = bcl_xstrdup(interp, proc_name);
        return;
    }

    handler = bcl_xcalloc(interp, 1, sizeof(*handler));
    handler->source = bcl_xstrdup(interp, source);
    handler->proc_name = bcl_xstrdup(interp, proc_name);
    handler->next = interp->handlers;
    interp->handlers = handler;
    buckets = handler_table(interp);
    bucket = handler_hash(source) % BCL_HANDLER_BUCKETS;
    handler->hash_next = buckets[bucket];
    buckets[bucket] = handler;
    interp->handler_count++;
}

int bcl_evtsys_unregister(bcl_interp_t *interp, const char *source) {
    bcl_handler_t *handler = handler_find(interp, source, NULL);
    bcl_handler_t **link;

    if (!handler) {
        return 0;
    }

    link = handler_list_link(interp, handler);
    if (link) {
        *link = handler->next;
    }
    handler_unlink_hash(interp, handler);

    free(handler->source);
    free(handler->proc_name);
    free(handler);
    interp->handler_count--;
    return 1;
}

const char *bcl_evtsys_lookup(bcl_interp_t *interp, const char *source) {
    bcl_handler_t *handler = handler_find(interp, source, NULL);
    return handler ? handler->proc_name : NULL;
}

int bcl_evtsys_pending(bcl_interp_t *interp) {
    return interp->handler_count > 0;
}

void bcl_evtsys_clear(bcl_interp_t *interp) {
    bcl_handler_t *handler = interp->handlers;

    while (handler) {
        bcl_handler_t *next = handler->next;
        free(handler->source);
        free(handler->proc_name);
        free(handler);
        handler = next;
    }

    interp->handlers = NULL;
    free(interp->handler_buckets);
    interp->handler_buckets = NULL;
    interp->handler_count = 0;
}
