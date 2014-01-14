/* wld: pixman.c
 *
 * Copyright (c) 2013, 2014 Michael Forney
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pixman.h"
#include "pixman-private.h"
#include "wld-private.h"

#define PIXMAN_COLOR(c) {                   \
    .alpha  = ((c >> 24) & 0xff) * 0x101,   \
    .red    = ((c >> 16) & 0xff) * 0x101,   \
    .green  = ((c >>  8) & 0xff) * 0x101,   \
    .blue   = ((c >>  0) & 0xff) * 0x101,   \
}

struct pixman_renderer
{
    struct wld_renderer base;
    pixman_image_t * target;
    pixman_glyph_cache_t * glyph_cache;
};

#include "interface/context.h"
#define RENDERER_IMPLEMENTS_REGION
#include "interface/renderer.h"
#include "interface/drawable.h"
IMPL(pixman, renderer)
IMPL(pixman, drawable)

static struct wld_context context = { .impl = &context_impl };
const struct wld_drawable_impl * const pixman_drawable_impl = &drawable_impl;

EXPORT
struct wld_context * wld_pixman_context = &context;

struct wld_renderer * context_create_renderer(struct wld_context * context)
{
    struct pixman_renderer * renderer;

    if (!(renderer = malloc(sizeof *renderer)))
        goto error0;

    if (!(renderer->glyph_cache = pixman_glyph_cache_create()))
        goto error1;

    renderer_initialize(&renderer->base, &renderer_impl);
    renderer->target = NULL;

    return &renderer->base;

  error1:
    free(renderer);
  error0:
    return NULL;
}

bool pixman_initialize_drawable
    (struct wld_context * context, struct pixman_drawable * drawable,
     uint32_t width, uint32_t height,
     void * data, uint32_t pitch, uint32_t format)
{
    drawable_initialize(&drawable->base, &drawable_impl,
                        width, height, format, pitch);
    drawable->context = (void *) context;
    drawable->image = pixman_image_create_bits(format_wld_to_pixman(format),
                                               width, height,
                                               (uint32_t *) data, pitch);

    return drawable->image != NULL;
}

struct wld_drawable * new_drawable(pixman_image_t * image)
{
    struct pixman_drawable * drawable;

    if (!(drawable = malloc(sizeof *drawable)))
        return NULL;

    drawable_initialize(&drawable->base, &drawable_impl,
                        pixman_image_get_width(image),
                        pixman_image_get_height(image),
                        format_pixman_to_wld(pixman_image_get_format(image)),
                        pixman_image_get_stride(image));
    drawable->base.map.data = pixman_image_get_data(image);
    drawable->image = image;

    return &drawable->base;
}

struct wld_drawable * context_create_drawable(struct wld_context * context,
                                              uint32_t width, uint32_t height,
                                              uint32_t format)
{
    struct wld_drawable * drawable;
    pixman_image_t * image;

    image = pixman_image_create_bits(format_wld_to_pixman(format),
                                     width, height, NULL, 0);

    if (!image)
        goto error0;

    if (!(drawable = new_drawable(image)))
        goto error1;

    return drawable;

  error1:
    pixman_image_unref(image);
  error0:
    return NULL;
}

struct wld_drawable * context_import(struct wld_context * context,
                                     uint32_t type, union wld_object object,
                                     uint32_t width, uint32_t height,
                                     uint32_t format, uint32_t pitch)
{
    struct wld_drawable * drawable;
    pixman_image_t * image;

    switch (type)
    {
        case WLD_OBJECT_DATA:
            image = pixman_image_create_bits(format_wld_to_pixman(format),
                                             width, height, object.ptr, pitch);
            break;
        default: image = NULL;
    }

    if (!image)
        goto error0;

    if (!(drawable = new_drawable(image)))
        goto error1;

    return drawable;

  error1:
    pixman_image_unref(image);
  error0:
    return NULL;

}

void context_destroy(struct wld_context * context)
{
}

uint32_t renderer_capabilities(struct wld_renderer * renderer,
                               struct wld_drawable * drawable)
{
    if (drawable->impl == &drawable_impl)
        return WLD_CAPABILITY_READ | WLD_CAPABILITY_WRITE;

    return 0;
}

bool renderer_set_target(struct wld_renderer * base,
                         struct wld_drawable * drawable)
{
    struct pixman_renderer * renderer = pixman_renderer(base);

    if (drawable && drawable->impl != &drawable_impl)
        return false;

    renderer->target = drawable ? pixman_drawable(drawable)->image : NULL;

    return true;
}

void renderer_fill_rectangle(struct wld_renderer * base, uint32_t color,
                             int32_t x, int32_t y,
                             uint32_t width, uint32_t height)
{
    struct pixman_renderer * renderer = pixman_renderer(base);
    pixman_color_t pixman_color = PIXMAN_COLOR(color);
    pixman_box32_t box = { x, y, x + width, y + height };

    pixman_image_fill_boxes(PIXMAN_OP_SRC, renderer->target,
                            &pixman_color, 1, &box);
}

void renderer_fill_region(struct wld_renderer * base, uint32_t color,
                          pixman_region32_t * region)
{
    struct pixman_renderer * renderer = pixman_renderer(base);
    pixman_color_t pixman_color = PIXMAN_COLOR(color);
    pixman_box32_t * boxes;
    int num_boxes;

    boxes = pixman_region32_rectangles(region, &num_boxes);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, renderer->target,
                            &pixman_color, num_boxes, boxes);
}

void renderer_copy_rectangle(struct wld_renderer * base,
                             struct wld_drawable * drawable,
                             int32_t dst_x, int32_t dst_y,
                             int32_t src_x, int32_t src_y,
                             uint32_t width, uint32_t height)
{
    struct pixman_renderer * renderer = pixman_renderer(base);
    pixman_image_t * dst = renderer->target, * src;

    if (drawable->impl != &drawable_impl)
        return;

    src = pixman_drawable(drawable)->image;
    pixman_image_composite32(PIXMAN_OP_SRC, src, NULL, dst,
                             src_x, src_y, 0, 0, dst_x, dst_y, width, height);
}

void renderer_copy_region(struct wld_renderer * base,
                          struct wld_drawable * drawable,
                          int32_t dst_x, int32_t dst_y,
                          pixman_region32_t * region)
{
    struct pixman_renderer * renderer = pixman_renderer(base);
    pixman_image_t * dst = renderer->target, * src;

    if (drawable->impl != &drawable_impl)
        return;

    src = pixman_drawable(drawable)->image;
    pixman_image_set_clip_region32(src, region);
    pixman_image_composite32(PIXMAN_OP_SRC, src, NULL, dst,
                             region->extents.x1, region->extents.y1, 0, 0,
                             region->extents.x1 + dst_x,
                             region->extents.y1 + dst_y,
                             region->extents.x2 - region->extents.x1,
                             region->extents.y2 - region->extents.y1);
    pixman_image_set_clip_region32(src, NULL);
}

static inline uint8_t reverse(uint8_t byte)
{
    byte = ((byte << 1) & 0xaa) | ((byte >> 1) & 0x55);
    byte = ((byte << 2) & 0xcc) | ((byte >> 2) & 0x33);
    byte = ((byte << 4) & 0xf0) | ((byte >> 4) & 0x0f);

    return byte;
}

void renderer_draw_text(struct wld_renderer * base,
                        struct font * font, uint32_t color,
                        int32_t x, int32_t y, const char * text, int32_t length,
                        struct wld_extents * extents)
{
    struct pixman_renderer * renderer = pixman_renderer(base);
    int ret;
    uint32_t c;
    struct glyph * glyph;
    FT_UInt glyph_index;
    pixman_glyph_t glyphs[strlen(text)];
    uint32_t index = 0, origin_x = 0;
    pixman_color_t pixman_color = PIXMAN_COLOR(color);
    pixman_image_t * solid;

    solid = pixman_image_create_solid_fill(&pixman_color);

    while ((ret = FcUtf8ToUcs4((FcChar8 *) text, &c, length)) > 0 && c != '\0')
    {
        text += ret;
        length -= ret;
        glyph_index = FT_Get_Char_Index(font->face, c);

        if (!font_ensure_glyph(font, glyph_index))
            continue;

        glyph = font->glyphs[glyph_index];

        glyphs[index].x = origin_x;
        glyphs[index].y = 0;
        glyphs[index].glyph = pixman_glyph_cache_lookup(renderer->glyph_cache,
                                                        font, glyph);

        /* If we don't have the glyph in our cache, do some conversions to make
         * pixman happy, and then insert it. */
        if (!glyphs[index].glyph)
        {
            uint8_t * src, * dst;
            uint32_t row, byte_index, bytes_per_row, pitch;
            pixman_image_t * image;
            FT_Bitmap * bitmap;

            bitmap = &glyph->bitmap;
            image = pixman_image_create_bits
                (PIXMAN_a1, bitmap->width, bitmap->rows, NULL, bitmap->pitch);

            if (!image)
                goto advance;

            pitch = pixman_image_get_stride(image);
            bytes_per_row = (bitmap->width + 7) / 8;
            src = bitmap->buffer;
            dst = (uint8_t *) pixman_image_get_data(image);

            for (row = 0; row < bitmap->rows; ++row)
            {
                /* Pixman's A1 format expects the bits in the opposite order
                 * that Freetype gives us. Sigh... */
                for (byte_index = 0; byte_index < bytes_per_row; ++byte_index)
                    dst[byte_index] = reverse(src[byte_index]);

                dst += pitch;
                src += bitmap->pitch;
            }

            /* Insert the glyph into the cache. */
            pixman_glyph_cache_freeze(renderer->glyph_cache);
            glyphs[index].glyph = pixman_glyph_cache_insert
                (renderer->glyph_cache, font, glyph,
                 -glyph->x, -glyph->y, image);
            pixman_glyph_cache_thaw(renderer->glyph_cache);

            /* The glyph cache copies the contents of the glyph bitmap. */
            pixman_image_unref(image);
        }

        ++index;

      advance:
        origin_x += glyph->advance;
    }

    pixman_composite_glyphs_no_mask(PIXMAN_OP_OVER, solid, renderer->target,
                                    0, 0, x, y, renderer->glyph_cache,
                                    index, glyphs);

    pixman_image_unref(solid);

    if (extents)
        extents->advance = origin_x;
}

void renderer_flush(struct wld_renderer * renderer)
{
}

void renderer_destroy(struct wld_renderer * base)
{
    struct pixman_renderer * renderer = pixman_renderer(base);

    pixman_glyph_cache_destroy(renderer->glyph_cache);
    free(renderer);
}

bool drawable_map(struct wld_drawable * drawable)
{
    return true;
}

bool drawable_unmap(struct wld_drawable * drawable)
{
    return true;
}

void drawable_destroy(struct wld_drawable * drawable)
{
    struct pixman_drawable * pixman = (void *) drawable;

    pixman_image_unref(pixman->image);
    free(pixman);
}

