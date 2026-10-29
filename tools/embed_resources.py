#!/usr/bin/env python3
"""Build a deterministic Win32 RCDATA table for a genuinely standalone EXE."""
import argparse
import json
from pathlib import Path


def embed(assets, shader, output):
    assets, shader, output = assets.resolve(), shader.resolve(), output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    emitters = json.loads((assets / 'maimai-fireworks.json').read_text(encoding='utf-8'))
    names = {'maimai-fireworks.json', 'ASSET-LICENSE.txt', 'answer.wav', 'tap.wav',
             'touch.wav', 'break-perfect.wav', 'break-critical.wav', 'touch-firework.wav'}
    for emitter in emitters:
        names.update(emitter['textures'])
    if any(e['material'] == 49 for e in emitters):
        names.add('rainbow.hlsl')
    files = {'effect.hlsl': shader}
    for name in names:
        if Path(name).name != name or ':' in name:
            raise ValueError('Unsafe resource name')
        path = assets / name
        if not path.is_file() or path.stat().st_size > 16 * 1024 * 1024:
            raise ValueError(f'Missing or oversized asset: {name}')
        files[name] = path
    repo = shader.parent.parent
    files['LICENSE.txt'] = repo / 'LICENSE'
    files['json.LICENSE.txt'] = repo / 'windows/vendor/json.LICENSE'
    table, rc = [], ['#pragma code_page(65001)', '#include <windows.h>']
    for index, (name, path) in enumerate(sorted(files.items()), 100):
        rc.append(f'{index} RCDATA "{path.as_posix()}"')
        table.append(f'    {{{json.dumps(name)}, {index}}},')
    (output / 'embedded.rc').write_text('\n'.join(rc) + '\n', encoding='utf-8')
    (output / 'embedded_table.h').write_text(
        '#pragma once\nstruct EmbeddedEntry { const char* name; int id; };\n'
        'inline constexpr EmbeddedEntry embeddedEntries[] = {\n' + '\n'.join(table) + '\n};\n', encoding='utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('assets', type=Path)
    p.add_argument('shader', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    embed(a.assets, a.shader, a.output)
