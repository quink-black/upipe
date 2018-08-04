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
 * @short Upipe GLFW sink module.
 */

#ifndef UPIPE_GL2_UPIPE_GLFW_SINK_H_
/** @hidden */
#define UPIPE_GL2_UPIPE_GLFW_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe-gl2/upipe_gl2_sink_common.h>

#define UPIPE_GLFW_SINK_SIGNATURE UBASE_FOURCC('g', 'l', 'f', 'w')

/** @This extends uprobe_event with specific events for glfw sink. */
enum uprobe_glfw_sink_event {
    UPROBE_GLFW_SINK_SENTINEL = UPROBE_GL2_SINK_LOCAL,

    UPROBE_GLFW_SINK_KEYPRESS,
    UPROBE_GLFW_SINK_KEYRELEASE,
};

/** @This extends upipe_command with specific commands for glfw sink. */
enum upipe_glfw_sink_command {
    UPIPE_GLFW_SINK_SENTINEL = UPIPE_GL2_SINK_CONTROL_LOCAL,

    /** launch glfw with window size and position (int, int, int, int) */
    UPIPE_GLFW_SINK_INIT,
    /** returns the current window size (int *, int *) */
    UPIPE_GLFW_SINK_GET_SIZE,
    /** set window size (int, int) */
    UPIPE_GLFW_SINK_SET_SIZE,
};

/** @This inits the glfw window/context and displays it
 *
 * @param upipe description structure of the pipe
 * @param x window position x
 * @param y window position y
 * @param width window width
 * @param height window height
 * @return false in case of error
 */
static inline bool upipe_glfw_sink_init(struct upipe *upipe, int x, int y,
                                       int width, int height)
{
    return upipe_control(upipe, UPIPE_GLFW_SINK_INIT, UPIPE_GLFW_SINK_SIGNATURE,
                         x, y, width, height);
}

/** @This returns the management structure for all glfw sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_glfw_sink_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
