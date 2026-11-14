#!/usr/bin/env python3
# svg2png.py - rasterize an SVG with the system librsvg + cairo (ctypes)
# Usage: svg2png.py in.svg out.png size
# This file is part of C64-EmuDis (GPL-2.0-or-later).
import ctypes, ctypes.util, sys

rsvg = ctypes.CDLL(ctypes.util.find_library("rsvg-2") or "librsvg-2.so.2")
cairo = ctypes.CDLL(ctypes.util.find_library("cairo") or "libcairo.so.2")

class Dim(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int),
                ("em", ctypes.c_double), ("ex", ctypes.c_double)]

rsvg.rsvg_handle_new_from_file.restype = ctypes.c_void_p
rsvg.rsvg_handle_new_from_file.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
rsvg.rsvg_handle_get_dimensions.argtypes = [ctypes.c_void_p, ctypes.POINTER(Dim)]
rsvg.rsvg_handle_render_cairo.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
cairo.cairo_image_surface_create.restype = ctypes.c_void_p
cairo.cairo_create.restype = ctypes.c_void_p
cairo.cairo_create.argtypes = [ctypes.c_void_p]
cairo.cairo_scale.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double]
cairo.cairo_surface_write_to_png.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]

src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
h = rsvg.rsvg_handle_new_from_file(src.encode(), None)
if not h:
    sys.exit("cannot load " + src)
d = Dim()
rsvg.rsvg_handle_get_dimensions(h, ctypes.byref(d))
surf = cairo.cairo_image_surface_create(0, size, size)   # ARGB32
cr = cairo.cairo_create(surf)
cairo.cairo_scale(cr, size / d.width, size / d.height)
rsvg.rsvg_handle_render_cairo(h, cr)
cairo.cairo_destroy(cr)
cairo.cairo_surface_write_to_png(surf, dst.encode())
cairo.cairo_surface_destroy(surf)
