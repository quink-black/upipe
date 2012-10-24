/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe module allowing to duplicate to several outputs
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_split_outputs.h>
#include <upipe/upipe_flows.h>
#include <upipe-modules/upipe_dup.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a dup pipe. */
struct upipe_dup {
    /** list of outputs */
    struct ulist outputs;

    /** list of input flows */
    struct ulist flows;
    /** true if we have thrown the ready event */
    bool ready;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_dup, upipe)

/** @internal @This is the private context of an output of a dup pipe. */
struct upipe_dup_output {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** suffix added to every flow on this output */
    char *flow_suffix;
    /** pipe acting as output */
    struct upipe *output;
};

/** We do not use @ref #UPIPE_HELPER_SPLIT_OUTPUT because it supposes there
 * is only one flow per output, which is not the case for us. */

/** @internal @This returns the uchain utility structure.
 *
 * @param s pointer to the output-specific substructure
 * @return pointer to the uchain utility structure
 */
static inline struct uchain *upipe_dup_output_to_uchain(struct upipe_dup_output *s)
{
    return &s->uchain;
}

/** @internal @This returns the private output-specific substructure.
 *
 * @param u uchain utility structure
 * @return pointer to the private STRUCTURE structure
 */
static inline struct upipe_dup_output *upipe_dup_output_from_uchain(struct uchain *u)
{
    return container_of(u, struct upipe_dup_output, uchain);
}

/** @internal @This checks if an output-specific substructure matches
 * a given flow suffix.
 *
 * @param output pointer to output-specific substructure
 * @param flow_suffix flow suffix
 * @return true if the substructure matches
 */
static inline bool upipe_dup_output_match(struct upipe_dup_output *output,
                                          const char *flow_suffix)
{
    assert(output != NULL);
    assert(flow_suffix != NULL);
    return !strcmp(output->flow_suffix, flow_suffix);
}

/** @internal @This allocates and initializes a new output-specific
 * substructure.
 *
 * @param upipe description structure of the pipe
 * @param flow_suffix flow suffix
 * @return pointer to allocated substructure
 */
static struct upipe_dup_output *upipe_dup_output_alloc(struct upipe *upipe,
                                                       const char *flow_suffix)
{
    assert(flow_suffix != NULL);
    struct upipe_dup_output *output = malloc(sizeof(struct upipe_dup_output));
    if (unlikely(output == NULL))
        return NULL;
    uchain_init(&output->uchain);
    output->flow_suffix = strdup(flow_suffix);
    if (unlikely(output->flow_suffix == NULL)) {
        free(output);
        return NULL;
    }
    output->output = NULL;
    return output;
}

/** @internal @This sends a uref to the output of a substructure.
 *
 * @param upipe description structure of the pipe
 * @param output pointer to output-specific substructure
 * @param uref uref structure to send
 */
static void upipe_dup_output_output(struct upipe *upipe,
                                    struct upipe_dup_output *output,
                                    struct uref *uref)
{
    if (unlikely(output->output == NULL))
        return;

    /* change flow */
    const char *flow_name;
    if (unlikely(!uref_flow_get_name(uref, &flow_name))) {
        if (unlikely(!uref_flow_set_name(uref, output->flow_suffix))) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            uref_free(uref);
            return;
        }
    } else {
        char new_flow[strlen(flow_name) + strlen(output->flow_suffix) + 2];
        sprintf(new_flow, "%s.%s", flow_name, output->flow_suffix);
        if (unlikely(!uref_flow_set_name(uref, new_flow))) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            uref_free(uref);
            return;
        }
    }
    upipe_input(output->output, uref);
}

/** @internal @This handles the get_output control command on a
 * substructure.
 *
 * @param upipe description structure of the pipe
 * @param output pointer to output-specific substructure
 * @param p filled in with the output
 * @return false in case of error
 */
static bool upipe_dup_output_get_output(struct upipe *upipe,
                                        struct upipe_dup_output *output,
                                        struct upipe **p)
{
    assert(p != NULL);
    *p = output->output;
    return true;
}

/** @internal @This handles the set_output control command on a
 * substructure, and properly deletes and replays flows on old and new
 * outputs.
 *
 * @param upipe description structure of the pipe
 * @param output pointer to output-specific substructure
 * @param o new output pipe
 * @return false in case of error
 */
static bool upipe_dup_output_set_output(struct upipe *upipe,
                                        struct upipe_dup_output *output,
                                        struct upipe *o)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    if (unlikely(output->output != NULL)) {
        /* change of output, signal flow deletions on old output */
        upipe_flows_foreach_delete(&upipe_dup->flows, upipe, uref,
                          upipe_dup_output_output(upipe, output, uref));
        upipe_release(output->output);
    }

    output->output = o;
    if (likely(o != NULL)) {
        upipe_use(o);
        /* replay flow definitions */
        upipe_flows_foreach_replay(&upipe_dup->flows, upipe, uref,
                          upipe_dup_output_output(upipe, output, uref));
    }
    return true;
}

/** @internal @This frees up an output-specific substructure.
 *
 * @param upipe description structure of the pipe
 * @param output substructure to free
 */
static void upipe_dup_output_free(struct upipe *upipe,
                                  struct upipe_dup_output *output)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    free(output->flow_suffix);
    if (likely(output->output != NULL)) {
        upipe_flows_foreach_delete(&upipe_dup->flows, upipe, uref,
                          upipe_dup_output_output(upipe, output, uref));
        upipe_release(output->output);
    }
    free(output);
}

UPIPE_HELPER_SPLIT_OUTPUTS(upipe_dup, outputs, upipe_dup_output)

/** @internal @This allocates a dup pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_dup_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe, struct ulog *ulog)
{
    struct upipe_dup *upipe_dup = malloc(sizeof(struct upipe_dup));
    if (unlikely(upipe_dup == NULL))
        return NULL;
    struct upipe *upipe = upipe_dup_to_upipe(upipe_dup);
    upipe_init(upipe, uprobe, ulog);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_DUP_SIGNATURE;
    urefcount_init(&upipe_dup->refcount);
    upipe_dup_init_outputs(upipe);
    upipe_flows_init(&upipe_dup->flows);
    upipe_dup->ready = false;
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_dup_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);

    if (unlikely(!upipe_flows_input(&upipe_dup->flows, upipe, uref))) {
        uref_free(uref);
        return false;
    }

    struct uchain *uchain;
    ulist_foreach (&upipe_dup->outputs, uchain) {
        struct upipe_dup_output *output = upipe_dup_output_from_uchain(uchain);
        struct uref *new_uref = uref_dup(uref);
        if (likely(new_uref != NULL))
            upipe_dup_output_output(upipe, output, new_uref);
        else {
            uref_free(uref);
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return false;
        }
    }
    uref_free(uref);
    return true;

    /* only to kill a gcc warning */
    upipe_dup_output(upipe, NULL, NULL);
}

/** @internal @This adds/deletes/changes an output. We cannot rely on
 * @ref upipe_dup_set_output since it only changes existing outputs.
 *
 * @param upipe description structure of the pipe
 * @param o pipe acting as output, or NULL to delete the output
 * @param flow_suffix flow suffix
 * @return false in case of error
 */
static bool _upipe_dup_set_output(struct upipe *upipe, struct upipe *o,
                                  const char *flow_suffix)
{
    struct upipe_dup_output *output;

    if (likely(o == NULL)) {
        ulog_debug(upipe->ulog, "deleting output: %s", flow_suffix);
        return upipe_dup_delete_output(upipe, flow_suffix,
                                       upipe_dup_output_free);

    } else if (likely(!upipe_dup_set_output(upipe, o, flow_suffix))) {
        ulog_debug(upipe->ulog, "adding output: %s", flow_suffix);
        output = upipe_dup_output_alloc(upipe, flow_suffix);
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return false;
        }
        upipe_dup_add_output(upipe, output);
        return upipe_dup_output_set_output(upipe, output, o);
    }

    return true;
}

/** @internal @This processes control commands on a dup pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_dup_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    switch (command) {
        case UPIPE_SPLIT_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_dup_get_output(upipe, p, flow_suffix);
        }
        case UPIPE_SPLIT_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            const char *flow_suffix = va_arg(args, const char *);
            return _upipe_dup_set_output(upipe, output, flow_suffix);
        }

        default:
            return false;
    }
}

/** @internal @This processes control commands on a dup source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_dup_control(struct upipe *upipe, enum upipe_command command,
                              va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_dup_input(upipe, uref);
    }

    if (unlikely(!_upipe_dup_control(upipe, command, args)))
        return false;

    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    if (likely(!upipe_dup->ready)) {
        upipe_dup->ready = true;
        upipe_throw_ready(upipe);
    }

    return true;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_use(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    urefcount_use(&upipe_dup->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dup_release(struct upipe *upipe)
{
    struct upipe_dup *upipe_dup = upipe_dup_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_dup->refcount))) {
        upipe_dup_clean_outputs(upipe, upipe_dup_output_free);
        upipe_flows_clean(&upipe_dup->flows);
        upipe_clean(upipe);
        urefcount_clean(&upipe_dup->refcount);
        free(upipe_dup);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_dup_mgr = {
    .upipe_alloc = upipe_dup_alloc,
    .upipe_control = upipe_dup_control,
    .upipe_use = upipe_dup_use,
    .upipe_release = upipe_dup_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all dup pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dup_mgr_alloc(void)
{
    return &upipe_dup_mgr;
}
