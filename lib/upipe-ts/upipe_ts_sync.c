/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module syncing on a transport stream
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_sync.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** default number of packets to sync with */
#define DEFAULT_TS_SYNC 2
/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."
/** when configured with standard TS size, we output TS packets */
#define OUTPUT_FLOW_DEF "block.mpegts."
/** otherwise there is a suffix to decaps */
#define SUFFIX_OUTPUT_FLOW_DEF "block.mpegtssuffix."
/** TS synchronization word */
#define TS_SYNC 0x47

/** @internal @This is the private context of a ts_sync pipe. */
struct upipe_ts_sync {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** TS packet size */
    size_t ts_size;
    /** number of packets to sync with */
    unsigned int ts_sync;
    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct ulist urefs;
    /** true if we have thrown the sync_acquired event */
    bool acquired;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_sync, upipe)
UPIPE_HELPER_SYNC(upipe_ts_sync, acquired)

UPIPE_HELPER_OUTPUT(upipe_ts_sync, output, flow_def, flow_def_sent)

/** @internal @This allocates a ts_sync pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_sync_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         struct ulog *ulog)
{
    struct upipe_ts_sync *upipe_ts_sync = malloc(sizeof(struct upipe_ts_sync));
    if (unlikely(upipe_ts_sync == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_sync_to_upipe(upipe_ts_sync);
    upipe_init(upipe, mgr, uprobe, ulog);
    upipe_ts_sync_init_sync(upipe);
    upipe_ts_sync_init_output(upipe);
    upipe_ts_sync->ts_size = TS_SIZE;
    upipe_ts_sync->ts_sync = DEFAULT_TS_SYNC;
    upipe_ts_sync->next_uref = NULL;
    ulist_init(&upipe_ts_sync->urefs);
    urefcount_init(&upipe_ts_sync->refcount);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This scans for a sync word in the working buffer.
 *
 * @param upipe description structure of the pipe
 * @param offset_p written with the offset of the first sync word, or the total
 * size of the working buffer if none was found
 * @return false if the working buffer doesn't contain any sync word
 */
static bool upipe_ts_sync_scan(struct upipe *upipe, size_t *offset_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    int offset = *offset_p;
    const uint8_t *buffer;
    int size = -1;
    while (uref_block_read(upipe_ts_sync->next_uref, offset, &size, &buffer)) {
        for (int i = 0; i < size; i++) {
            if (buffer[i] == TS_SYNC) {
                uref_block_unmap(upipe_ts_sync->next_uref, offset, size);
                *offset_p += i;
                return true;
            }
        }
        uref_block_unmap(upipe_ts_sync->next_uref, offset, size);
        *offset_p += size;
        offset += size;
        size = -1;
    }
    return false;
}

/** @internal @This checks the presence of the required number of sync words
 * in the working buffer.
 *
 * @param upipe description structure of the pipe
 * @param offset_p written with the offset of the potential first TS packet in
 * the working buffer
 * @return false if not enough sync words could be tested
 */
static bool upipe_ts_sync_check(struct upipe *upipe, size_t *offset_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    for ( ; ; ) {
        if (unlikely(!upipe_ts_sync_scan(upipe, offset_p)))
            return false;

        /* first octet at *offset_p is a sync word */
        int ts_sync = upipe_ts_sync->ts_sync - 1;
        for (int offset = *offset_p + upipe_ts_sync->ts_size; ts_sync;
             ts_sync--, offset += upipe_ts_sync->ts_size) {
            const uint8_t *buffer;
            int size = 1;
            uint8_t word;
            if (unlikely(!uref_block_read(upipe_ts_sync->next_uref,
                                          offset, &size, &buffer)))
                /* not enough sync words could be tested */
                return false;
            assert(size == 1);
            word = *buffer;
            uref_block_unmap(upipe_ts_sync->next_uref, offset, size);
            if (word != TS_SYNC) {
                *offset_p += 1;
                break;
            }
        }
        if (!ts_sync)
            break;
    }

    return true;
}

/** @internal @This appends a new uref to the list of received urefs, and
 * also appends it to the uref we are currently working on.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure to append
 */
static void upipe_ts_sync_append(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (upipe_ts_sync->next_uref != NULL) {
        struct ubuf *ubuf = ubuf_dup(uref->ubuf);
        if (unlikely(ubuf == NULL ||
                     !uref_block_append(upipe_ts_sync->next_uref, ubuf))) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            uref_free(uref);
            if (ubuf != NULL)
                ubuf_free(ubuf);
        } else
            ulist_add(&upipe_ts_sync->urefs, uref_to_uchain(uref));
    } else {
        upipe_ts_sync->next_uref = uref;
        uref_block_size(upipe_ts_sync->next_uref,
                        &upipe_ts_sync->next_uref_size);
    }
}

/** @internal @This consumed the given number of octets from the input
 * buffers, and rotate the buffers accordingly.
 *
 * @param upipe description structure of the pipe
 * @param consumed number of octets consumed from the input buffers
 */
static void upipe_ts_sync_consume(struct upipe *upipe, size_t consumed)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    while (consumed) {
        assert(upipe_ts_sync->next_uref != NULL);
        if (consumed < upipe_ts_sync->next_uref_size) {
            uref_block_resize(upipe_ts_sync->next_uref, consumed, -1);
            upipe_ts_sync->next_uref_size -= consumed;
            break;
        }

        consumed -= upipe_ts_sync->next_uref_size;
        uref_free(upipe_ts_sync->next_uref);
        upipe_ts_sync->next_uref = NULL;

        struct ulist urefs = upipe_ts_sync->urefs;
        ulist_init(&upipe_ts_sync->urefs);
        struct uchain *uchain;
        while ((uchain = ulist_pop(&urefs)) != NULL)
            upipe_ts_sync_append(upipe, uref_from_uchain(uchain));
    }
}

/** @internal @This tries to find TS packets in the buffered input urefs.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_ts_sync_work(struct upipe *upipe, struct upump *upump)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    while (upipe_ts_sync->next_uref != NULL) {
        size_t offset = 0;
        bool ret = upipe_ts_sync_check(upipe, &offset);
        if (offset) {
            upipe_ts_sync_sync_lost(upipe);
            upipe_ts_sync_consume(upipe, offset);
        }
        if (!ret)
            break;

        /* upipe_ts_sync_check said there is at least one TS packet there. */
        upipe_ts_sync_sync_acquired(upipe);
        struct uref *output = uref_dup(upipe_ts_sync->next_uref);
        upipe_ts_sync_consume(upipe, upipe_ts_sync->ts_size);
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            continue;
        }
        uref_block_resize(output, 0, upipe_ts_sync->ts_size);
        upipe_ts_sync_output(upipe, output, upump);
    }
}

/** @internal @This flushes all input buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_ts_sync_flush(struct upipe *upipe, struct upump *upump)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (upipe_ts_sync->acquired) {
        size_t offset = 0, size;
        while (upipe_ts_sync->next_uref != NULL &&
               uref_block_size(upipe_ts_sync->next_uref, &size) &&
               size >= upipe_ts_sync->ts_size &&
               upipe_ts_sync_scan(upipe, &offset) && !offset) {
            struct uref *output = uref_dup(upipe_ts_sync->next_uref);
            upipe_ts_sync_consume(upipe, upipe_ts_sync->ts_size);
            if (unlikely(output == NULL)) {
                ulog_aerror(upipe->ulog);
                upipe_throw_aerror(upipe);
                continue;
            }
            uref_block_resize(output, 0, upipe_ts_sync->ts_size);
            upipe_ts_sync_output(upipe, output, upump);
        }
    }

    if (upipe_ts_sync->next_uref != NULL) {
        uref_free(upipe_ts_sync->next_uref);
        upipe_ts_sync->next_uref = NULL;

        struct uchain *uchain;
        while ((uchain = ulist_pop(&upipe_ts_sync->urefs)) != NULL)
            uref_free(uref_from_uchain(uchain));
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_sync_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_ts_sync_flush(upipe, upump);

        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW_DEF))) {
            uref_free(uref);
            upipe_ts_sync_store_flow_def(upipe, NULL);
            upipe_throw_flow_def_error(upipe, uref);
            return;
        }

        ulog_debug(upipe->ulog, "flow definition: %s", def);
        /* FIXME make it dependant on the output size */
        uref_flow_set_def(uref, OUTPUT_FLOW_DEF);
        upipe_ts_sync_store_flow_def(upipe, uref);
        return;
    }

    if (unlikely(upipe_ts_sync->flow_def == NULL)) {
        uref_free(uref);
        upipe_throw_flow_def_error(upipe, uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_ts_sync_append(upipe, uref);
    upipe_ts_sync_work(upipe, upump);
}

/** @internal @This returns the configured size of TS packets.
 *
 * @param upipe description structure of the pipe
 * @param size_p filled in with the configured size, in octets
 * @return false in case of error
 */
static bool _upipe_ts_sync_get_size(struct upipe *upipe, int *size_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    assert(size_p != NULL);
    *size_p = upipe_ts_sync->ts_size;
    return true;
}

/** @internal @This sets the configured size of TS packets. Common values are:
 * @table 2
 * @item size (in octets) @item description
 * @item 188 @item standard size of TS packets according to ISO/IEC 13818-1
 * @item 196 @item TS packet followed by an 8-octet timestamp or checksum
 * @item 204 @item TS packet followed by a 16-octet checksum
 * @end table
 *
 * @param upipe description structure of the pipe
 * @param size configured size, in octets
 * @return false in case of error
 */
static bool _upipe_ts_sync_set_size(struct upipe *upipe, int size)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (size < 0)
        return false;
    upipe_ts_sync->ts_size = size;
    /* FIXME change flow definition */
    return true;
}

/** @internal @This returns the configured number of packets to synchronize
 * with.
 *
 * @param upipe description structure of the pipe
 * @param sync_p filled in with number of packets
 * @return false in case of error
 */
static bool _upipe_ts_sync_get_sync(struct upipe *upipe, int *sync_p)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    assert(sync_p != NULL);
    *sync_p = upipe_ts_sync->ts_sync;
    return true;
}

/** @internal @This sets the configured number of packets to synchronize with.
 * The higher the value, the slower the synchronization, but the fewer false
 * positives. The minimum (and default) value is 2.
 *
 * @param upipe description structure of the pipe
 * @param sync number of packets
 * @return false in case of error
 */
static bool _upipe_ts_sync_set_sync(struct upipe *upipe, int sync)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (sync < DEFAULT_TS_SYNC)
        return false;
    upipe_ts_sync->ts_sync = sync;
    return true;
}

/** @internal @This processes control commands on a ts sync pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_sync_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_sync_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_sync_set_output(upipe, output);
        }

        case UPIPE_TS_SYNC_GET_SIZE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int *size_p = va_arg(args, int *);
            return _upipe_ts_sync_get_size(upipe, size_p);
        }
        case UPIPE_TS_SYNC_SET_SIZE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int size = va_arg(args, int);
            return _upipe_ts_sync_set_size(upipe, size);
        }
        case UPIPE_TS_SYNC_GET_SYNC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int *sync_p = va_arg(args, int *);
            return _upipe_ts_sync_get_sync(upipe, sync_p);
        }
        case UPIPE_TS_SYNC_SET_SYNC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_SYNC_SIGNATURE);
            int sync = va_arg(args, int);
            return _upipe_ts_sync_set_sync(upipe, sync);
        }
        default:
            return false;
    }
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sync_use(struct upipe *upipe)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    urefcount_use(&upipe_ts_sync->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sync_release(struct upipe *upipe)
{
    struct upipe_ts_sync *upipe_ts_sync = upipe_ts_sync_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_sync->refcount))) {
        upipe_throw_dead(upipe);

        upipe_ts_sync_flush(upipe, NULL);
        upipe_ts_sync_clean_output(upipe);
        upipe_ts_sync_clean_sync(upipe);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_sync->refcount);
        free(upipe_ts_sync);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_sync_mgr = {
    .signature = UPIPE_TS_SYNC_SIGNATURE,

    .upipe_alloc = upipe_ts_sync_alloc,
    .upipe_input = upipe_ts_sync_input,
    .upipe_control = upipe_ts_sync_control,
    .upipe_use = upipe_ts_sync_use,
    .upipe_release = upipe_ts_sync_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all ts_sync pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_sync_mgr_alloc(void)
{
    return &upipe_ts_sync_mgr;
}