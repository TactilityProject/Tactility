// SPDX-License-Identifier: Apache-2.0
#include <lvgl/binfont.h>

#include <binfont/binfont.h>
#include <tactility/memory.h>

static bool get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, uint32_t letter, uint32_t letter_next) {
    struct BinFont* binfont = (struct BinFont*)font->dsc;
    BinFontGlyph glyph;
    if (!binfont_get_glyph(binfont, letter, &glyph)) {
        return false;
    }

    int32_t advance_x16 = (int32_t)glyph.advance_x16;
    BinFontGlyph next;
    if (font->kerning == LV_FONT_KERNING_NORMAL && letter_next != 0 && binfont_get_glyph(binfont, letter_next, &next)) {
        advance_x16 += binfont_get_kerning_x16(binfont, glyph.glyph_id, next.glyph_id);
    }

    dsc->adv_w = (uint16_t)((advance_x16 + 8) >> 4);
    dsc->box_w = glyph.width;
    dsc->box_h = glyph.height;
    dsc->ofs_x = glyph.x;
    dsc->ofs_y = glyph.y;
    dsc->stride = 0;
    dsc->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc->is_placeholder = false;
    dsc->gid.index = glyph.glyph_id;
    return true;
}

static const void* get_glyph_bitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t* draw_buf) {
    if (dsc->box_w == 0 || dsc->box_h == 0) {
        return NULL;
    }
    struct BinFont* binfont = (struct BinFont*)dsc->resolved_font->dsc;
    const BinFontGlyph glyph = {
        .glyph_id = dsc->gid.index,
        .width = dsc->box_w,
        .height = dsc->box_h,
    };
    if (binfont_get_glyph_bitmap(binfont, &glyph, draw_buf->data, draw_buf->header.stride) != ERROR_NONE) {
        return NULL;
    }
    lv_draw_buf_flush_cache(draw_buf, NULL);
    return draw_buf;
}

lv_font_t* lvgl_binfont_create(struct BinFont* font) {
    lv_font_t* lv_font = memory_calloc(1, sizeof(lv_font_t));
    if (lv_font == NULL) {
        return NULL;
    }

    BinFontMetrics metrics;
    binfont_get_metrics(font, &metrics);

    lv_font->get_glyph_dsc = get_glyph_dsc;
    lv_font->get_glyph_bitmap = get_glyph_bitmap;
    lv_font->line_height = metrics.line_height;
    lv_font->base_line = metrics.base_line;
    lv_font->subpx = LV_FONT_SUBPX_NONE;
    lv_font->kerning = LV_FONT_KERNING_NORMAL;
    lv_font->underline_position = (int8_t)metrics.underline_position;
    lv_font->underline_thickness = (int8_t)metrics.underline_thickness;
    lv_font->dsc = font;
    return lv_font;
}

void lvgl_binfont_destroy(lv_font_t* font) {
    memory_free(font);
}
