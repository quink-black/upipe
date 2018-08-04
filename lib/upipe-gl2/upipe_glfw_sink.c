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
 * @short Upipe GLFW sink module
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-gl2/upipe_glfw_sink.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#define GLFW_INCLUDE_NONE 1
#include <GLFW/glfw3.h>

/** max number of urefs to buffer */
#define BUFFER_UREFS 5

/** @hidden */
static bool upipe_glfw_sink_output(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);
/** @hidden */
static void upipe_glfw_sink_write_watcher(struct upump *upump);

/** @internal upipe_glfw_sink private structure */
struct upipe_glfw_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** delay applied to pts attribute when uclock is provided */
    uint64_t latency;
    GLFWwindow *window;

    /** frame counter */
    uint64_t counter;
    /** theta */
    float theta;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** event watcher */
    struct upump *upump_watcher;
    /** write watcher */
    struct upump *upump;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_glfw_sink, upipe, UPIPE_GLFW_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_glfw_sink, urefcount, upipe_glfw_sink_free)
UPIPE_HELPER_VOID(upipe_glfw_sink)
UPIPE_HELPER_UPUMP_MGR(upipe_glfw_sink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_glfw_sink, upump, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_glfw_sink, upump_watcher, upump_mgr)
UPIPE_HELPER_INPUT(upipe_glfw_sink, urefs, nb_urefs, max_urefs, blockers, upipe_glfw_sink_output)
UPIPE_HELPER_UCLOCK(upipe_glfw_sink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

static void upipe_glfw_sink_framebuffer_size_cb(GLFWwindow *window, int width, int height)
{
    struct upipe_glfw_sink *upipe_glfw_sink = glfwGetWindowUserPointer(window);

    if (upipe_glfw_sink) {
        struct upipe *upipe = upipe_glfw_sink_to_upipe(upipe_glfw_sink);

        upipe_throw(upipe, UPROBE_GL2_SINK_RESHAPE, UPIPE_GL2_SINK_SIGNATURE, width, height);
        glfwSwapBuffers(window);
    }
}

static void upipe_glfw_sink_key_cb(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    struct upipe_glfw_sink *upipe_glfw_sink = glfwGetWindowUserPointer(window);

    if (upipe_glfw_sink) {
        struct upipe *upipe = upipe_glfw_sink_to_upipe(upipe_glfw_sink);

        if (action == GLFW_PRESS)
            upipe_throw(upipe, UPROBE_GLFW_SINK_KEYPRESS, UPIPE_GLFW_SINK_SIGNATURE, key);
        else if (action == GLFW_RELEASE)
            upipe_throw(upipe, UPROBE_GLFW_SINK_KEYRELEASE, UPIPE_GLFW_SINK_SIGNATURE, key);
    }
}

static void upipe_glfw_sink_watcher_cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_glfw_sink *upipe_glfw_sink = upipe_glfw_sink_from_upipe(upipe);
    glfwPollEvents();
    if (glfwWindowShouldClose(upipe_glfw_sink->window)) {
        upipe_throw(upipe, UPROBE_GLFW_SINK_WINDOW_CLOSE, UPIPE_GLFW_SINK_SIGNATURE);
    }
}

static bool upipe_glfw_sink_init_watcher(struct upipe *upipe)
{
    struct upipe_glfw_sink *upipe_glfw_sink = upipe_glfw_sink_from_upipe(upipe);
    if (upipe_glfw_sink->upump_mgr) {
        struct upump *upump = upump_alloc_timer(upipe_glfw_sink->upump_mgr,
                upipe_glfw_sink_watcher_cb, upipe, upipe->refcount,
                27000000/1000, 27000000/1000);
        if (unlikely(!upump)) {
            return false;
        } else {
            upipe_glfw_sink_set_upump_watcher(upipe, upump);
            upump_start(upump);
        }
    }
    return true;
}

static int upipe_glfw_sink_init_glfw(struct upipe *upipe, int x, int y, int width, int height)
{
    struct upipe_glfw_sink *upipe_glfw_sink = upipe_glfw_sink_from_upipe(upipe);
    GLFWwindow *window = NULL;

    if (glfwInit() != GLFW_TRUE) {
        upipe_err(upipe, "Could not init glfw");
        return UBASE_ERR_EXTERNAL;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    window = glfwCreateWindow(width, height, "upipe glfw", NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        upipe_err(upipe, "Could not create window");
        return UBASE_ERR_EXTERNAL;
    }
    upipe_glfw_sink->window = window;

    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, upipe_glfw_sink);
    glfwSetFramebufferSizeCallback(window, upipe_glfw_sink_framebuffer_size_cb);
    glfwSetKeyCallback(window, upipe_glfw_sink_key_cb);
    glfwSwapBuffers(window);

    // Now init GL context
    upipe_throw(upipe, UPROBE_GL2_SINK_INIT,
                UPIPE_GL2_SINK_SIGNATURE, width, height);

    upipe_glfw_sink_check_upump_mgr(upipe);
    upipe_glfw_sink_init_watcher(upipe);

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    upipe_glfw_sink_framebuffer_size_cb(window, w, h);
    return UBASE_ERR_NONE;
}

static void upipe_glfw_sink_clean_glfw(struct upipe *upipe)
{
    struct upipe_glfw_sink *upipe_glfw_sink = upipe_glfw_sink_from_upipe(upipe);
    glfwDestroyWindow(upipe_glfw_sink->window);
    glfwTerminate();
}

static struct upipe *upipe_glfw_sink_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_glfw_sink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_glfw_sink *upipe_glfw_sink = upipe_glfw_sink_from_upipe(upipe);
    upipe_glfw_sink_init_urefcount(upipe);
    upipe_glfw_sink_init_upump_mgr(upipe);
    upipe_glfw_sink_init_upump(upipe);
    upipe_glfw_sink_init_upump_watcher(upipe);
    upipe_glfw_sink_init_input(upipe);
    upipe_glfw_sink_init_uclock(upipe);
    upipe_glfw_sink->max_urefs = BUFFER_UREFS;
    upipe_glfw_sink->latency = 0;
    upipe_glfw_sink->window = NULL;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This handles input pics.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static bool upipe_glfw_sink_output(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_glfw_sink *upipe_glfw_sink = upipe_glfw_sink_from_upipe(upipe);

    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_glfw_sink->latency = 0;
        uref_clock_get_latency(uref, &upipe_glfw_sink->latency);
        /* FIXME throw new flow definition to update probe */
        upipe_throw_new_flow_def(upipe, uref);

        uref_free(uref);
        return true;
    }

    if (likely(upipe_glfw_sink->uclock != NULL)) {
        uint64_t pts = 0;
        if (likely(ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
            pts += upipe_glfw_sink->latency;
            uint64_t now = uclock_now(upipe_glfw_sink->uclock);
            if (now < pts) {
                upipe_verbose_va(upipe, "sleeping %"PRIu64" (%"PRIu64")",
                                 pts - now, pts);
                upipe_glfw_sink_wait_upump(upipe, pts - now,
                                          upipe_glfw_sink_write_watcher);
                return false;
            } else if (now > pts + UCLOCK_FREQ / 10) {
                upipe_warn_va(upipe, "late picture dropped (%"PRId64")",
                              (now - pts) * 1000 / UCLOCK_FREQ);
                uref_free(uref);
                return true;
            }
        } else
            upipe_warn(upipe, "received non-dated buffer");
    }

    glfwMakeContextCurrent(upipe_glfw_sink->window);
    upipe_gl2_sink_throw_render(upipe, uref);
    glfwSwapBuffers(upipe_glfw_sink->window);
    uref_free(uref);
    return true;
}

/** @internal @This is called when the picture should be displayed.
 *
 * @param upump description structure of the watcher
 */
static void upipe_glfw_sink_write_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_glfw_sink_set_upump(upipe, NULL);
    upipe_glfw_sink_output_input(upipe);
    upipe_glfw_sink_unblock_input(upipe);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_glfw_sink_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_glfw_sink *upipe_glfw_sink = upipe_glfw_sink_from_upipe(upipe);

    if (!upipe_glfw_sink_check_input(upipe) ||
        !upipe_glfw_sink_output(upipe, uref, upump_p)) {
        upipe_glfw_sink_hold_input(upipe, uref);
        upipe_glfw_sink_block_input(upipe, upump_p);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_glfw_sink_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    /* for the moment we only support rgb24 */
    uint8_t macropixel;
    if (!ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)) ||
        macropixel != 1 ||
        (!ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "r5g6b5")) &&
         !ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 3, "r8g8b8")))) {
        upipe_err(upipe, "incompatible flow definition");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_INVALID;
    }

    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_glfw_sink_provide_flow_format(struct upipe *upipe,
                                              struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    bool rgb565 = ubase_check(uref_pic_flow_check_chroma(flow_format, 1, 1, 2, "r5g6b5"));

    uref_pic_flow_clear_format(flow_format);
    uref_pic_flow_set_macropixel(flow_format, 1);
    uref_pic_flow_set_planes(flow_format, 0);
    if (rgb565)
        uref_pic_flow_add_plane(flow_format, 1, 1, 2, "r5g6b5");
    else
        uref_pic_flow_add_plane(flow_format, 1, 1, 3, "r8g8b8");
    uref_pic_set_progressive(flow_format);
    uref_pic_flow_delete_colour_primaries(flow_format);
    uref_pic_flow_delete_transfer_characteristics(flow_format);
    uref_pic_flow_delete_matrix_coefficients(flow_format);
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_glfw_sink_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_glfw_sink_set_upump(upipe, NULL);
            upipe_glfw_sink_set_upump_watcher(upipe, NULL);
            UBASE_RETURN(upipe_glfw_sink_attach_upump_mgr(upipe))
            upipe_glfw_sink_init_watcher(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_ATTACH_UCLOCK:
            upipe_glfw_sink_set_upump(upipe, NULL);
            upipe_glfw_sink_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_glfw_sink_provide_flow_format(upipe, request);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_glfw_sink_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_glfw_sink_get_max_length(upipe, p);
        }
        case UPIPE_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_glfw_sink_set_max_length(upipe, max_length);
        }

        case UPIPE_GLFW_SINK_INIT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GLFW_SINK_SIGNATURE)
            int x = va_arg(args, int);
            int y = va_arg(args, int);
            int width = va_arg(args, int);
            int height = va_arg(args, int);
            return upipe_glfw_sink_init_glfw(upipe, x, y, width, height);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_glfw_sink_free(struct upipe *upipe)
{
    struct upipe_glfw_sink *upipe_glfw_sink = upipe_glfw_sink_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_glfw_sink_clean_upump(upipe);
    upipe_glfw_sink_clean_upump_watcher(upipe);
    upipe_glfw_sink_clean_upump_mgr(upipe);
    upipe_glfw_sink_clean_glfw(upipe);
    upipe_glfw_sink_clean_uclock(upipe);
    upipe_glfw_sink_clean_input(upipe);
    upipe_glfw_sink_clean_urefcount(upipe);
    upipe_glfw_sink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_glfw_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_GLFW_SINK_SIGNATURE,

    .upipe_alloc = upipe_glfw_sink_alloc,
    .upipe_input = upipe_glfw_sink_input,
    .upipe_control = upipe_glfw_sink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for glfw_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_glfw_sink_mgr_alloc(void)
{
    return &upipe_glfw_sink_mgr;
}
