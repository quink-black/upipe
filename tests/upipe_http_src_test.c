/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short unit test for http source
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_null.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define READ_SIZE 4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_READ_END:
        case UPROBE_NEED_INPUT:
            break;
    }
    return true;
}

int main(int argc, char *argv[])
{
    const char *url;

    if (argc < 2) {
        fprintf(stdout, "Usage: %s <url>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    url = argv[1];

    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, -1,
                                                         -1, 0);
    assert(ubuf_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    assert(upump_mgr != NULL);
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_DEBUG);
    assert(log != NULL);

    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    struct upipe *upipe_null = upipe_alloc(upipe_null_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "null"));

    struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
    assert(upipe_http_src_mgr != NULL);
    struct upipe *upipe_http_src = upipe_alloc(upipe_http_src_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "http"));
    assert(upipe_http_src != NULL);
    assert(upipe_set_upump_mgr(upipe_http_src, upump_mgr));
    assert(upipe_set_uref_mgr(upipe_http_src, uref_mgr));
    assert(upipe_set_ubuf_mgr(upipe_http_src, ubuf_mgr));
    assert(upipe_source_set_read_size(upipe_http_src, READ_SIZE));
    assert(upipe_set_uclock(upipe_http_src, uclock));
    assert(upipe_http_src_set_url(upipe_http_src, url));
    assert(upipe_set_output(upipe_http_src, upipe_null));
    upipe_release(upipe_null);

    ev_loop(loop, 0);

    upipe_release(upipe_http_src);
    upipe_mgr_release(upipe_http_src_mgr); // nop
    upipe_mgr_release(upipe_null_mgr); // nop

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    ev_default_destroy();
    return 0;
}