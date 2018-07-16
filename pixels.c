/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 FastLED
 * Copyright (c) 2018 Ayke van Laethem
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// A lot of code has been copied from FastLED and was modified to work with
// MicroPython. Some changes:
//   * removing some preprocessor macros
//   * removing AVR-specific code
//   * code style

#include "py/obj.h"
#include "py/runtime.h"

static int pixels_scale8(int i, int frac) {
    return (i * (1+frac)) >> 8;
}

///  The "video" version of scale8 guarantees that the output will
///  be only be zero if one or both of the inputs are zero.  If both
///  inputs are non-zero, the output is guaranteed to be non-zero.
///  This makes for better 'video'/LED dimming, at the cost of
///  several additional cycles.
static uint8_t pixels_scale8_video(int i, int scale) {
    uint8_t j = ((i * scale) >> 8) + ((i && scale) ? 1 : 0);
    // uint8_t nonzeroscale = (scale != 0) ? 1 : 0;
    // uint8_t j = (i == 0) ? 0 : (((int)i * (int)(scale) ) >> 8) + nonzeroscale;
    return j;
}

static uint32_t pixels_hsv2rgb_rainbow(uint8_t hue, uint8_t sat, uint8_t val) {
    // Yellow has a higher inherent brightness than
    // any other color; 'pure' yellow is perceived to
    // be 93% as bright as white.  In order to make
    // yellow appear the correct relative brightness,
    // it has to be rendered brighter than all other
    // colors.
    // Level Y1 is a moderate boost, the default.
    // Level Y2 is a strong boost.
    const uint8_t Y1 = 1;
    const uint8_t Y2 = 0;

    // G2: Whether to divide all greens by two.
    // Depends GREATLY on your particular LEDs
    const uint8_t G2 = 0;

    uint8_t offset = hue & 0x1F; // 0..31

    // offset8 = offset * 8
    uint8_t offset8 = offset << 3;

    uint8_t third = pixels_scale8( offset8, (256 / 3)); // max = 85

    // 32-bit math takes less code space and is ~1% faster
    uint32_t r, g, b;

    if( ! (hue & 0x80) ) {
        // 0XX
        if( ! (hue & 0x40) ) {
            // 00X
            //section 0-1
            if( ! (hue & 0x20) ) {
                // 000
                //case 0: // R -> O
                r = 255 - third;
                g = third;
                b = 0;
            } else {
                // 001
                //case 1: // O -> Y
                if( Y1 ) {
                    r = 171;
                    g = 85 + third ;
                    b = 0;
                }
                if( Y2 ) {
                    r = 170 + third;
                    //uint8_t twothirds = (third << 1);
                    uint8_t twothirds = pixels_scale8( offset8, ((256 * 2) / 3)); // max=170
                    g = 85 + twothirds;
                    b = 0;
                }
            }
        } else {
            //01X
            // section 2-3
            if( !  (hue & 0x20) ) {
                // 010
                //case 2: // Y -> G
                if( Y1 ) {
                    //uint8_t twothirds = (third << 1);
                    uint8_t twothirds = pixels_scale8( offset8, ((256 * 2) / 3)); // max=170
                    r = 171 - twothirds;
                    g = 170 + third;
                    b = 0;
                }
                if( Y2 ) {
                    r = 255 - offset8;
                    g = 255;
                    b = 0;
                }
            } else {
                // 011
                // case 3: // G -> A
                r = 0;
                g = 255 - third;
                b = third;
            }
        }
    } else {
        // section 4-7
        // 1XX
        if( ! (hue & 0x40) ) {
            // 10X
            if( ! ( hue & 0x20) ) {
                // 100
                //case 4: // A -> B
                r = 0;
                //uint8_t twothirds = (third << 1);
                uint8_t twothirds = pixels_scale8( offset8, ((256 * 2) / 3)); // max=170
                g = 171 - twothirds; //170?
                b = 85  + twothirds;

            } else {
                // 101
                //case 5: // B -> P
                r = third;
                g = 0;
                b = 255 - third;

            }
        } else {
            if( !  (hue & 0x20)  ) {
                // 110
                //case 6: // P -- K
                r = 85 + third;
                g = 0;
                b = 171 - third;

            } else {
                // 111
                //case 7: // K -> R
                r = 170 + third;
                g = 0;
                b = 85 - third;

            }
        }
    }

    // This is one of the good places to scale the green down,
    // although the client can scale green down as well.
    if( G2 ) g = g >> 1;

    // Scale down colors if we're desaturated at all
    // and add the brightness_floor to r, g, and b.
    if( sat != 255 ) {
        if( sat == 0) {
            r = 255; b = 255; g = 255;
        } else {
            //nscale8x3_video( r, g, b, sat);
            if( r ) r = pixels_scale8( r, sat);
            if( g ) g = pixels_scale8( g, sat);
            if( b ) b = pixels_scale8( b, sat);

            uint8_t desat = 255 - sat;
            desat = pixels_scale8( desat, desat);

            uint8_t brightness_floor = desat;
            r += brightness_floor;
            g += brightness_floor;
            b += brightness_floor;
        }
    }

    // Now scale everything down if we're at value < 255.
    if( val != 255 ) {

        val = pixels_scale8_video( val, val);
        if( val == 0 ) {
            r=0; g=0; b=0;
        } else {
            // nscale8x3_video( r, g, b, val);
            if( r ) r = pixels_scale8( r, val);
            if( g ) g = pixels_scale8( g, val);
            if( b ) b = pixels_scale8( b, val);
        }
    }

    return r << 16 | g << 8 | b;
}

static mp_obj_t pixels_hsv2rgb_rainbow_(mp_obj_t hue_in, mp_obj_t sat_in, mp_obj_t val_in) {
    mp_int_t hue = mp_obj_get_int(hue_in);
    mp_int_t sat = mp_obj_get_int(sat_in);
    mp_int_t val = mp_obj_get_int(val_in);
    uint32_t color = pixels_hsv2rgb_rainbow(hue, sat, val);
    return mp_obj_new_int(color);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pixels_hsv2rgb_rainbow_obj, pixels_hsv2rgb_rainbow_);

static const mp_rom_map_elem_t pixels_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pixels) },
    { MP_ROM_QSTR(MP_QSTR_hsv2rgb_rainbow), MP_ROM_PTR(&pixels_hsv2rgb_rainbow_obj) },
};
static MP_DEFINE_CONST_DICT(pixels_module_globals, pixels_module_globals_table);

const mp_obj_module_t pixels_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&pixels_module_globals,
};
