#!/usr/bin/env python3
# Copyright PeakRacing
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Encode the pinned suite's indexed BMP fixture as ordinary GBA tile data.

No graphics optimizations: preserve palette indices and use literal-only LZ77.
The core never links this host-side asset generator.
"""
import pathlib
import struct
import sys


def literal_lz(data):
    result = bytearray(struct.pack('<I', len(data) << 8 | 0x10))
    for offset in range(0, len(data), 8):
        result.append(0)
        result.extend(data[offset:offset + 8])
    result.extend(b'\0' * (-len(result) % 4))
    return result


def main():
    source, output = map(pathlib.Path, sys.argv[1:])
    bmp = source.read_bytes()
    offset = struct.unpack_from('<I', bmp, 10)[0]
    width, height = struct.unpack_from('<ii', bmp, 18)
    depth = struct.unpack_from('<H', bmp, 28)[0]
    compression = struct.unpack_from('<I', bmp, 30)[0]
    if width != 256 or height != 128 or depth != 4 or compression:
        raise ValueError('unexpected pinned video fixture layout')
    stride = (width * depth + 31) // 32 * 4
    pixels = []
    for y in range(height):
        row = bmp[offset + (height - 1 - y) * stride:offset + (height - y) * stride]
        pixels.append([row[x // 2] >> (4 if x % 2 == 0 else 0) & 15 for x in range(width)])
    tiles = bytearray()
    tile_map = bytearray()
    # Tile zero must be blank: the expected function clears the lower map.
    tiles.extend(b'\0' * 32)
    for y in range(0, height, 8):
        for x in range(0, width, 8):
            tile_map.extend(struct.pack('<H', len(tiles) // 32))
            for dy in range(8):
                for dx in range(0, 8, 2):
                    tiles.append(pixels[y + dy][x + dx] | pixels[y + dy][x + dx + 1] << 4)
    palette = bytearray()
    colors = (offset - 54) // 4
    for index in range(16):
        b, g, r = bmp[54 + index * 4:57 + index * 4] if index < colors else (0, 0, 0)
        palette.extend(struct.pack('<H', r // 8 | g // 8 << 5 | b // 8 << 10))
    arrays = {'Tiles': literal_lz(tiles), 'Map': literal_lz(tile_map), 'Pal': palette}
    lines = ['/* Generated from the fixed MIT-licensed suite fixture. */', '#pragma once', '#include <stdint.h>']
    for suffix, data in arrays.items():
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        lines.append('static const uint32_t degenerateObjTransform' + suffix + '[] = {')
        for start in range(0, len(words), 8):
            lines.append('    ' + ', '.join('0x%08xu' % word for word in words[start:start + 8]) + ',')
        lines.append('};')
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
