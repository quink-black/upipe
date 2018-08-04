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
 * @short Upipe GL2 sink animation
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe-gl2/uprobe_gl2_sink.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <epoxy/gl.h>

/** @This is the private structure for gl2 sink renderer probe. */
struct uprobe_gl2_sink {
    GLuint program;
    GLuint vbo;
    GLuint ebo;
    GLuint texture;

    GLint pos_loc;
    GLint tex_coord_loc;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_gl2_sink, uprobe);

/** @internal @This reshapes the gl2 view upon receiving an Exposure event
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param w window width
 * @param h window height
 */
static void uprobe_gl2_sink_reshape(struct uprobe *uprobe,
                                   struct upipe *upipe,
                                   int w, int h)
{
    glViewport(0, 0, w, h);
}

/** @internal @This updates the probe with latest flow definition
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void uprobe_gl2_sink_new_flow(struct uprobe *uprobe,
                                    struct upipe *upipe, struct uref *uref)
{
    struct uprobe_gl2_sink *uprobe_gl2_sink =
        uprobe_gl2_sink_from_uprobe(uprobe);
}

static bool upipe_gl2_texture_load_uref(struct uref *uref, GLuint texture)
{
    const uint8_t *data = NULL;
    bool rgb565 = false;
    size_t width, height, stride;
    uint8_t msize;
    uref_pic_size(uref, &width, &height, NULL);
    if (!ubase_check(uref_pic_plane_read(uref, "r8g8b8", 0, 0, -1, -1,
                                         &data)) ||
        !ubase_check(uref_pic_plane_size(uref, "r8g8b8", &stride,
                                         NULL, NULL, &msize))) {
        if (!ubase_check(uref_pic_plane_read(uref, "r5g6b5", 0, 0, -1, -1,
                                             &data)) ||
            !ubase_check(uref_pic_plane_size(uref, "r5g6b5", &stride,
                                             NULL, NULL, &msize))) {
            return false;
        }
        rgb565 = true;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
            rgb565 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE, data);
    uref_pic_plane_unmap(uref, rgb565 ? "r5g6b5" : "r8g8b8", 0, 0, -1, -1);

    return true;
}
/** @internal @This does the actual rendering upon receiving a pic
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return an error code
 */
static int uprobe_gl2_sink_render(struct uprobe *uprobe,
                                 struct upipe *upipe, struct uref *uref)
{
    struct uprobe_gl2_sink *uprobe_gl2_sink = uprobe_gl2_sink_from_uprobe(uprobe);

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(uprobe_gl2_sink->program);

    /* load image to texture */
    if (!upipe_gl2_texture_load_uref(uref, uprobe_gl2_sink->texture)) {
        upipe_err(upipe, "Could not map picture plane");
        return UBASE_ERR_EXTERNAL;
    }

    /*
    glBindBuffer(GL_ARRAY_BUFFER, uprobe_gl2_sink->vbo);
    glVertexAttribPointer(uprobe_gl2_sink->pos_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(uprobe_gl2_sink->pos_loc);
    glVertexAttribPointer(uprobe_gl2_sink->tex_coord_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(uprobe_gl2_sink->tex_coord_loc);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, uprobe_gl2_sink->ebo);
    */

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    return 0;
}

/** @internal @This does the gl2 (window-system non-specific) init
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param w pic width
 * @param h pic height
 */
static void uprobe_gl2_sink_init2(struct uprobe *uprobe,
                                 struct upipe *upipe,
                                 int w, int h)
{
    struct uprobe_gl2_sink *uprobe_gl2_sink =
        uprobe_gl2_sink_from_uprobe(uprobe);

    const char *vertex_shader_src =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_tex_coord;\n"
        "varying vec2 tex_coord;\n"
        "void main()\n"
        "{\n"
        "   gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "   tex_coord = a_tex_coord;\n"
        "}";
    const char *frag_shader_src =
        "varying vec2 tex_coord;\n"
        "uniform sampler2D texture1;\n"
        "void main()\n"
        "{\n"
        "   gl_FragColor = texture2D(texture1, tex_coord);\n"
        "}";

    GLuint vertex, fragment;

    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertex_shader_src, NULL);
    glCompileShader(vertex);
    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &frag_shader_src, NULL);
    glCompileShader(fragment);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLuint vbo, ebo;
    GLfloat vertices[] = {
        -1.0f, 1.0f, 0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 0.0f,
        1.0f, -1.0f, 1.0f, 1.0f,
    };
    GLuint indices[] = {
        0, 1, 2,
        2, 1, 3,
    };

    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    GLint pos_loc, tex_coord_loc;
    pos_loc = glGetAttribLocation(program, "a_pos");
    tex_coord_loc = glGetAttribLocation(program, "a_tex_coord");

    glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(tex_coord_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(tex_coord_loc);

    glEnable(GL_CULL_FACE);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    uprobe_gl2_sink->program = program;
    uprobe_gl2_sink->vbo = vbo;
    uprobe_gl2_sink->ebo = ebo;
    uprobe_gl2_sink->texture = texture;
    uprobe_gl2_sink->pos_loc = pos_loc;
    uprobe_gl2_sink->tex_coord_loc = tex_coord_loc;

    glClearColor (0.0, 0.0, 0.0, 0.0);
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_gl2_sink_throw(struct uprobe *uprobe,
                                struct upipe *upipe,
                                int event, va_list args)
{
    switch (event) {
        case UPROBE_NEW_FLOW_DEF: { /* FIXME */
            struct uref *uref = va_arg(args, struct uref*);
            uprobe_gl2_sink_new_flow(uprobe, upipe, uref);
            return UBASE_ERR_NONE;
        }
        case UPROBE_GL2_SINK_INIT: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL2_SINK_SIGNATURE);
            int w = va_arg(args, int);
            int h = va_arg(args, int);
            uprobe_gl2_sink_init2(uprobe, upipe, w, h);
            return UBASE_ERR_NONE;
        }
        case UPROBE_GL2_SINK_RENDER: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL2_SINK_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            return uprobe_gl2_sink_render(uprobe, upipe, uref);
        }
        case UPROBE_GL2_SINK_RESHAPE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL2_SINK_SIGNATURE);
            int w = va_arg(args, int);
            int h = va_arg(args, int);
            uprobe_gl2_sink_reshape(uprobe, upipe, w, h);
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** @internal @This initializes a new uprobe_gl2_sink structure.
 *
 * @param uprobe_gl2_sink pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
static struct uprobe *
uprobe_gl2_sink_init(struct uprobe_gl2_sink *uprobe_gl2_sink,
                    struct uprobe *next)
{
    assert(uprobe_gl2_sink != NULL);
    struct uprobe *uprobe = uprobe_gl2_sink_to_uprobe(uprobe_gl2_sink);

    uprobe_gl2_sink->program = 0;
    uprobe_gl2_sink->texture = 0;
    uprobe_init(uprobe, uprobe_gl2_sink_throw, next);
    return uprobe;
}

/** @internal @This cleans up a uprobe_gl2_sink structure.
 *
 * @param uprobe_gl2_sink structure to free
 */
static void uprobe_gl2_sink_clean(struct uprobe_gl2_sink *uprobe_gl2_sink)
{
    glDeleteTextures(1, &uprobe_gl2_sink->texture);
    glDeleteBuffers(1, &uprobe_gl2_sink->vbo);
    glDeleteBuffers(1, &uprobe_gl2_sink->ebo);
    glDeleteProgram(uprobe_gl2_sink->program);
    struct uprobe *uprobe = &uprobe_gl2_sink->uprobe;
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_gl2_sink)
#undef ARGS
#undef ARGS_DECL
