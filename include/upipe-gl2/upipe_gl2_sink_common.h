/*
 * Copyright (C) 2018 Zhao Zhili <quinkblack@foxmail.com>
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
 * @short Upipe GL2 - common definitions
 */

#ifndef UPIPE_GL2_UPIPE_GL2_SINK_COMMON_H_
/** @hidden */
#define UPIPE_GL2_UPIPE_GL2_SINK_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_GL2_SINK_SIGNATURE UBASE_FOURCC('g', 'l', '2', ' ')

/** @This extends uprobe_event with specific events for gl2 sink. */
enum uprobe_gl2_sink_event {
    UPROBE_GL2_SINK_SENTINEL = UPROBE_LOCAL,

    /** init GL2 context (int SIGNATURE, int width, int height) */
    UPROBE_GL2_SINK_INIT,
    /** render GL2 (int SIGNATURE, struct uref*) */
    UPROBE_GL2_SINK_RENDER,
    /** reshape GL2 (int SIGNATURE, int width, int height) */
    UPROBE_GL2_SINK_RESHAPE,

    UPROBE_GL2_SINK_LOCAL
};

/** @This throws an UPROBE_GL2_SINK_RENDER event.
 *
 * @param upipe pointer to pipe throwing the event
 * @param uref uref structure describing the picture
 * @return an error code
 */
static int upipe_gl2_sink_throw_render(struct upipe *upipe, struct uref *uref)
{
    return upipe_throw(upipe, UPROBE_GL2_SINK_RENDER,
                       UPIPE_GL2_SINK_SIGNATURE, uref);
}

/** @This extends upipe_command with specific commands for gl2 sink. */
enum upipe_gl2_sink_command {
    UPIPE_GL2_SINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_GL2_SINK_CONTROL_LOCAL
};

#ifdef __cplusplus
}
#endif
#endif
