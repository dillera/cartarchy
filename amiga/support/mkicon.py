#!/usr/bin/env python3
"""Write Cartarchy.info: a Workbench 1.3 tool icon from art/icon.png.

The .info format is the classic DiskObject: magic, a Gadget whose render
is one Image, then the planar image data and the Tool Types. Everything is
big-endian; pointer fields only need to be non-zero where data follows.
The PNG's pens are Workbench 1.3's: 0 blue, 1 white, 2 black, 3 orange.

Usage: mkicon.py <icon.png> <output.info>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from png2planes import read_png  # noqa: E402

STACK = 16384


def planes(img, w, depth=2):
    words = (w + 15) // 16
    out = bytearray()
    for plane in range(depth):
        for row in img:
            bits = [(pen >> plane) & 1 for pen in row] + [0] * (words * 16 - w)
            for k in range(words):
                value = 0
                for b in bits[k * 16:(k + 1) * 16]:
                    value = (value << 1) | b
                out += struct.pack(">H", value)
    return bytes(out)


def disk_object(w, h):
    gadget = struct.pack(
        ">IhhhhHHHIIIiIHI",
        0,            # NextGadget
        0, 0, w, h,   # LeftEdge, TopEdge, Width, Height
        0x0004,       # Flags: GADGIMAGE, highlight by complement
        0x0003,       # Activation: RELVERIFY | GADGIMMEDIATE
        0x0001,       # GadgetType: BOOLGADGET
        1,            # GadgetRender: an Image follows
        0, 0, 0, 0, 0, 0,
    )
    header = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIIi",
        3, 0,              # do_Type WBTOOL, pad
        0,                 # DefaultTool: none
        1,                 # ToolTypes: present
        -0x80000000,       # CurrentX: NO_ICON_POSITION
        -0x80000000,       # CurrentY
        0, 0,              # DrawerData, ToolWindow
        STACK,
    )
    assert len(header) == 78
    return header


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    w, h, _, rows = read_png(sys.argv[1])
    image = struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0)
    tool_types = struct.pack(">I", 4)          # an empty list
    with open(sys.argv[2], "wb") as f:
        f.write(disk_object(w, h) + image + planes(rows, w) + tool_types)


if __name__ == "__main__":
    main()
