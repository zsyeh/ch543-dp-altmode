#!/usr/bin/env python3
"""Describe the two currently built Board2 images; never treat stale HEX as current."""
import argparse
import hashlib
import json
from pathlib import Path
from check_vectors import read_hex

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('images', type=Path, nargs=2)
p.add_argument('--vbus-verified', type=int, choices=(0, 1), required=True)
a = p.parse_args()
label = 'vbus-verified' if a.vbus_verified else 'guarded'
items = []
for image in a.images:
    assert image.name.endswith(f'-{label}.hex')
    ihx = image.with_suffix('.ihx')
    assert read_hex(image) == read_hex(ihx), 'HEX differs from linked image'
    resource = json.loads(image.with_suffix('.resources.json').read_text())
    assert resource['sha256'] == hashlib.sha256(ihx.read_bytes()).hexdigest()
    items.append(dict(file=image.name, sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                      board='Board2 CH9445F', dp_video_role='source' if '-source-' in image.name else 'sink',
                      pd_power_role='sink', initial_usb_data_role='UFP',
                      vbus_verified_build_flag=a.vbus_verified, hardware_validated=False,
                      flash=resource['flash'], iram=resource['iram_occupied'],
                      xram=resource['xram'], stack_capacity=resource['stack_capacity']))
dest = a.images[0].parent / f'images-{label}.json'
dest.write_text(json.dumps(items, indent=2) + '\n')
print(f'Board2 image manifest: {dest}; PD attach ' + ('requires qualified hardware' if a.vbus_verified else 'DISABLED'))
legacy = list(Path('build').glob('ch543-dp-*.hex')) + list(Path('build/board2').glob('ch543-dp-*.hex'))
if legacy:
    print('STALE AMBIGUOUS IMAGES FOUND; do not flash these. Run make clean before rebuilding:')
    for image in legacy:
        print(f'  {image}')
