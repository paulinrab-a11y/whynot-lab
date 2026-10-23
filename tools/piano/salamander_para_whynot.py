"""Monta o piano da WHYNOT a partir do Salamander Grand Piano V3 (44,1 kHz / 16 bits, CC BY 3.0).

Lê o SalamanderGrandPianoV3.sfz, escolhe 8 das 16 camadas de força (as pares, cada uma cobrindo também
a faixa da ímpar de baixo), copia só os arquivos usados para a pasta da biblioteca e grava o manifesto
whynot-piano.json que o app lê, mais o CREDITOS.txt que a licença pede.

Uso: python -I salamander_para_whynot.py <pasta do Salamander extraído> <pasta da biblioteca>

A pasta extraída é a que tem SalamanderGrandPianoV3.sfz e a subpasta 44.1khz16bit.
Arquivo de origem: freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPianoV3+20161209_44khz16bit.tar.xz
"""
import json
import os
import re
import shutil
import sys

NOTE_NAMES = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}


def parse_sfz(text):
    """Devolve as regiões com os opcodes do grupo herdados."""
    regions = []
    group = {}
    for raw in text.splitlines():
        line = raw.split('//', 1)[0].strip()
        if not line:
            continue
        tags = re.split(r'(<\w+>)', line)
        current = None
        for part in tags:
            part = part.strip()
            if not part:
                continue
            if part == '<group>':
                group = {}
                current = group
                continue
            if part == '<region>':
                current = dict(group)
                regions.append(current)
                continue
            if current is None:
                continue
            # "sample=" pode ter espaço no nome: pega até o próximo opcode
            for key, value in re.findall(r'(\w+)=(.*?)(?=\s+\w+=|$)', part):
                current[key] = value.strip()
    return [r for r in regions if 'sample' in r]


def main():
    src, dest = sys.argv[1], sys.argv[2]
    sfz = os.path.join(src, 'SalamanderGrandPianoV3.sfz')
    regions = parse_sfz(open(sfz, encoding='latin-1').read())
    zones = []
    used = set()

    # notas: as camadas pares, cada uma com a faixa de força dela e da ímpar de baixo
    notes = [r for r in regions if r.get('trigger') != 'release' and re.match(r'.*v(\d+)\.wav$', r['sample'])
             and r.get('lokey', '0') != '-1']
    by_root = {}
    for r in notes:
        layer = int(re.match(r'.*v(\d+)\.wav$', r['sample']).group(1))
        by_root.setdefault(int(r.get('pitch_keycenter', '60')), {})[layer] = r   # sem pitch_keycenter, o SFZ usa 60

    for root, layers in sorted(by_root.items()):
        for even in range(2, 17, 2):
            r = layers.get(even)
            below = layers.get(even - 1)
            if r is None:
                continue
            lovel = int((below or r).get('lovel', '1'))
            hivel = int(r.get('hivel', '127'))
            zones.append({
                'file': os.path.basename(r['sample'].replace('\\', '/')),
                'root': root, 'lo': int(r['lokey']), 'hi': int(r['hikey']),
                'lovel': lovel, 'hivel': hivel, 'trigger': 'attack',
                'veltrack': float(r.get('amp_veltrack', '100')) / 100.0,
                'releaseSeconds': float(r.get('ampeg_release', '1')),
                'volumeDb': float(r.get('volume', '0')),
            })

    # releases: ressonância das cordas (harmL/harmS) e o martelo voltando (rel1..rel88)
    for r in regions:
        if r.get('trigger') != 'release':
            continue
        key = int(r['lokey'])
        root = int(r.get('pitch_keycenter', '60'))
        zones.append({
            'file': os.path.basename(r['sample'].replace('\\', '/')),
            'root': root, 'lo': key, 'hi': int(r.get('hikey', key)),
            'lovel': int(r.get('lovel', '1')), 'hivel': int(r.get('hivel', '127')), 'trigger': 'release',
            'veltrack': float(r.get('amp_veltrack', '100')) / 100.0,
            'volumeDb': float(r.get('volume', '0')),
            'rtDecay': float(r.get('rt_decay', '0')),
            'keytrack': float(r.get('pitch_keytrack', '100')) / 100.0,
        })

    samples_dir = os.path.join(dest, 'samples')
    os.makedirs(samples_dir, exist_ok=True)
    total = 0

    for z in zones:
        name = z['file']
        if name in used:
            continue
        used.add(name)
        origin = os.path.join(src, '44.1khz16bit', name)
        target = os.path.join(samples_dir, name)
        if not os.path.exists(target) or os.path.getsize(target) != os.path.getsize(origin):
            shutil.copyfile(origin, target)
        total += os.path.getsize(target)

    manifest = {
        'format': 'whynot-piano',
        'version': 1,
        'name': 'WHYNOT Grand',
        'source': 'Salamander Grand Piano V3 (Yamaha C5), 44,1 kHz / 16 bits',
        'credit': 'Salamander Grand Piano V3, gravado por Alexander Holm, licença CC BY 3.0. '
                  'Modificado para a WHYNOT LAB: 8 das 16 camadas de força, sem o ruído do pedal.',
        'license': 'CC BY 3.0 (https://creativecommons.org/licenses/by/3.0/)',
        'samples': 'samples',
        'zones': zones,
    }
    with open(os.path.join(dest, 'whynot-piano.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)

    with open(os.path.join(dest, 'CREDITOS.txt'), 'w', encoding='utf-8') as f:
        f.write(manifest['credit'] + '\n' + manifest['license'] + '\n'
                'Origem: https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html\n')

    attacks = sum(1 for z in zones if z['trigger'] == 'attack')
    print(f'{len(zones)} zonas ({attacks} de nota, {len(zones) - attacks} de release), '
          f'{len(used)} arquivos, {total / 1024 / 1024:.1f} MB em {dest}')


if __name__ == '__main__':
    main()
