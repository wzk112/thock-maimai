#!/usr/bin/env python3
"""Import a lawfully obtained local Mac resource folder into a Windows pack.

No resources are downloaded. Output is ignored by Git. The original media and
any translated shader keep their own rights; this script's MIT license is not
a license to distribute those resources.
"""
import argparse
import json
import re
import shutil
import zipfile
from pathlib import Path


def convert_shader(source):
    # Syntax adapter for the existing locally generated SpriteKit shader.
    source = source[source.index('void main()'):]
    source = source.replace('void main() {', 'float4 RainbowPixel(Vertex input):SV_TARGET {')
    source = source.replace('u_time', 'frame.z').replace('v_tex_coord', 'float2(input.uv.x,1-input.uv.y)')
    source = source.replace('gl_FragCoord.xy/a_resolution', 'float2(input.position.x/frame.x,1-input.position.y/frame.y)')
    source = source.replace('a_tint', 'tint')
    source = re.sub(r'\bvec([234])\b', r'float\1', source)
    # GLSL accepts float4(scalar) splats; make them explicit HLSL casts.
    pattern = re.compile(r'float4\(')
    position = 0
    while match := pattern.search(source, position):
        start = match.end()
        depth, commas, end = 1, 0, start
        while depth:
            char = source[end]
            if char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
            elif char == ',' and depth == 1:
                commas += 1
            end += 1
        if commas == 0:
            replacement = '((float4)(' + source[start:end-1] + '))'
            source = source[:match.start()] + replacement + source[end:]
            position = match.start() + len(replacement)
        else:
            position = start
    source = source.replace('fract(', 'frac(').replace('mix(', 'lerp(')
    source = source.replace('gl_FragColor=', 'return ')
    return '// Locally imported game-derived shader; not covered by the app MIT license.\n'+source


def prepare(source, output):
    source, output = source.resolve(), output.resolve()
    if source == output or source in output.parents:
        raise ValueError('Choose a separate output directory')
    output.mkdir(parents=True, exist_ok=True)
    emitters = json.loads((source/'maimai-fireworks.json').read_text())
    names = {'maimai-fireworks.json'}
    for emitter in emitters:
        names.update(emitter['textures'])
    for name in names:
        if Path(name).name != name or ':' in name:
            raise ValueError('Unsafe asset filename')
        shutil.copy2(source/name, output/name)
    if any(e['material'] == 49 for e in emitters):
        (output/'rainbow.hlsl').write_text(convert_shader((source/'maimai-rainbow.fsh').read_text()), encoding='utf-8')
    sounds = {'answer.wav', 'tap.wav', 'touch.wav', 'break-perfect.wav', 'break-critical.wav', 'touch-firework.wav'}
    with zipfile.ZipFile(source/'maimai-packs.zip') as pack:
        for sound in sounds:
            candidates = [name for name in pack.namelist() if Path(name).name == sound and not name.startswith('__MACOSX/')]
            if not candidates:
                raise FileNotFoundError(sound)
            (output/sound).write_bytes(pack.read(candidates[0]))
    (output/'ASSET-LICENSE.txt').write_text('Locally supplied game resources. Retain original rights and distribution terms.\nNot covered by the application MIT license.\n', encoding='utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    prepare(a.source, a.output)
