/*
 *  sortchars     analyse ASCII characters
 *  Copyright (c) 2007 Sam Hocevar <sam@zoy.org>
 *                All Rights Reserved
 *
 *  $Id$
 *
 *  This program is free software. It comes without any warranty, to
 *  the extent permitted by applicable law. You can redistribute it
 *  and/or modify it under the terms of the Do What The Fuck You Want
 *  To Public License, Version 2, as published by Sam Hocevar. See
 *  http://sam.zoy.org/wtfpl/COPYING for more details.
 */

#include "config.h"

#if !defined(__KERNEL__)
#   include <stdio.h>
#   include <string.h>
#   include <stdlib.h>
#endif

#include "cucul.h"

#define GLYPHS 0x7f
#define FONT 0 /* 0 or 1 */
#define DX 2
#define DY 3
#define RANGEBITS 2
#define RANGE (1 << RANGEBITS)
#define FULLRANGE (1 << (RANGEBITS * DX * DY))

int total[GLYPHS][DX][DY];
int16_t allbits[GLYPHS];
int bestchar[FULLRANGE];

static int curve[17] = /* 17 instead of 16 */
{
    0, 4, 6, 8, 9, 10, 11, 12, 12, 13, 13, 14, 14, 15, 15, 15, 15
    //0, 3, 5, 7, 8, 9, 10, 11, 12, 13, 13, 14, 14, 15, 15, 15, 15
    //0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15
};

static int distance(uint16_t, uint16_t);
static void testcircle(void);

int main(int argc, char *argv[])
{
    int count[DX][DY];
    char utf8[7];
    cucul_canvas_t *cv;
    cucul_font_t *f;
    char const * const * fonts;
    uint8_t *img;
    unsigned int w, h, x, y;
    int ret, i, max;

    /* Load a libcucul internal font */
    fonts = cucul_get_font_list();
    if(fonts[FONT] == NULL)
    {
        fprintf(stderr, "error: libcucul was compiled without any fonts\n");
        return -1;
    }
    f = cucul_load_font(fonts[FONT], 0);
    if(f == NULL)
    {
        fprintf(stderr, "error: could not load font \"%s\"\n", fonts[0]);
        return -1;
    }

    cv = cucul_create_canvas(1, 1);
    cucul_set_color_ansi(cv, CUCUL_WHITE, CUCUL_BLACK);

    /* Create our bitmap buffer (32-bit ARGB) */
    w = cucul_get_font_width(f);
    h = cucul_get_font_height(f);
    img = malloc(4 * w * h);

    /* Zero our structures */
    for(y = 0; y < DY; y++)
        for(x = 0; x < DX; x++)
            count[x][y] = 0;

    for(y = 0; y < h; y++)
        for(x = 0; x < w; x++)
            count[x * DX / w][y * DY / h]++;

    for(i = 0x20; i < GLYPHS; i++)
        for(y = 0; y < DY; y++)
            for(x = 0; x < DX; x++)
                total[i][x][y] = 0;

    /* Draw all glyphs and count their pixels */
    for(i = 0x20; i < GLYPHS; i++)
    {
        cucul_put_char(cv, 0, 0, i);

        /* Render the canvas onto our image buffer */
        cucul_render_canvas(cv, f, img, w, h, 4 * w);

        for(y = 0; y < h * DY; y++)
            for(x = 0; x < w * DX; x++)
                total[i][x / w][y / h]
                  += img[(w * (y / DY) + (x / DX)) * 4 + 1];
    }

    /* Compute max pixel value */
    max = 0;
    for(i = 0x20; i < GLYPHS; i++)
        for(y = 0; y < DY; y++)
            for(x = 0; x < DX; x++)
            {
                int val = total[i][x][y] * 256 / count[x][y];
                if(val > max)
                    max = val;
            }

    /* Compute bits for all glyphs */
    for(i = 0x20; i < GLYPHS; i++)
    {
        int bits = 0;

        if(i >= 0x7f && i <= 0x9f)
        {
            allbits[i] = 0;
            continue;
        }

        for(y = 0; y < DY; y++)
        {
            for(x = 0; x < DX; x++)
            {
                int t = total[i][x][y] * 16 * 256 / (count[x][y] * max);
                bits *= RANGE;
                bits |= curve[t] / (16 / RANGE);
            }
        }

        allbits[i] = bits;
    }

    /* Find a glyph for all combinations */
    for(i = 0; i < FULLRANGE; i++)
    {
        int j, mindist = 0x1000, best = 0;

        for(j = 0x20; j < GLYPHS; j++)
        {
            int d = distance(i, allbits[j]);
            if(d < mindist)
            {
                best = j;
                mindist = d;
                if(d == 0)
                    break;
            }
        }

        bestchar[i] = best;
    }

    /* Print results */
    printf("/* Generated by sortchars.c */\n");
    printf("static char const cells_to_ascii[%i] =\n{\n    ", FULLRANGE);
    for(i = 0; i < FULLRANGE; i++)
    {
        ret = cucul_utf32_to_utf8(utf8, bestchar[i]);
        utf8[ret] = '\0';
        printf("%i, ", bestchar[i]);
        if((i % 16) == 15 && i != FULLRANGE - 1)
            printf("\n    ");
    }
    printf("\n};\n\n");
    printf("static uint16_t const ascii_to_cells[%i] =\n{\n    ", GLYPHS);
    for(i = 0; i < GLYPHS; i++)
    {
        ret = cucul_utf32_to_utf8(utf8, bestchar[i]);
        utf8[ret] = '\0';
        printf("0x%03x, ", allbits[i]);
        if((i % 8) == 7 && i != GLYPHS - 1)
            printf("\n    ");
    }
    printf("\n};\n");

    cucul_free_canvas(cv);

    testcircle();

    return 0;
}

static int distance(uint16_t mychar, uint16_t x)
{
    int i, d = 0;

    for(i = 0; i < DX * DY; i++)
    {
        int t = (int)(mychar & (RANGE - 1)) - (int)(x & (RANGE - 1));
        d += t > 0 ? 1 * t : -2 * t;
        mychar /= RANGE;
        x /= RANGE;
    }

    return d;
}

#define WIDTH 40
#define HEIGHT 18

static void testcircle(void)
{
    char utf8[7];
    uint8_t *buf = malloc(256 * 256);
    uint16_t *dst = malloc(WIDTH * DX * HEIGHT * DY * sizeof(uint16_t));
    int x, y, ret;

    memset(buf, 0, 256 * 256);
    memset(dst, 0, WIDTH * DX * HEIGHT * DY);

    /* Fill image */
    for(y = 0; y < 256; y++)
        for(x = 0; x < 256; x++)
        {
            int dist2 = (x - 128) * (x - 128) + (y - 128) * (y - 128);
            if(dist2 < 25000 && dist2 > 18000)
                buf[y * 256 + x] = 255;
            else if(dist2 < 14000 && dist2 > 9000)
                buf[y * 256 + x] = 204;
            else if(dist2 < 6000 && dist2 > 3000)
                buf[y * 256 + x] = 153;
            else if(dist2 < 1600 && dist2 > 300)
                buf[y * 256 + x] = 102;
        }

    /* Parse image */
    for(y = 0; y < HEIGHT * DY; y++)
        for(x = 0; x < WIDTH * DX; x++)
            dst[y * WIDTH * DX + x] = (int)buf[(y * 256 / (HEIGHT * DY)) * 256 + (x * 256 / (WIDTH * DX))] * RANGE / 256;

    printf("/* example:\n");
    for(y = 0; y < HEIGHT; y++)
    {
        for(x = 0; x < WIDTH; x++)
        {
            uint16_t bits = 0;
            int i, j;
            for(j = 0; j < DY; j++)
                for(i = 0; i < DX; i++)
                {
                    bits *= RANGE;
                    bits |= dst[(y * DY + j) * WIDTH * DX + x * DX + i];
                }

            ret = cucul_utf32_to_utf8(utf8, bestchar[bits]);
            utf8[ret] = '\0';
            printf("%s", utf8);
        }

        printf("\n");
    }
    printf("*/\n");
}

