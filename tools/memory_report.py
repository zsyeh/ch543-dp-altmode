#!/usr/bin/env python3
"""Report actual SDCC allocation, preserving overlap and stack distinctions."""
import collections
import hashlib
import json
import pathlib
import re
import sys

p = pathlib.Path(sys.argv[1])
mem = p.with_suffix('.mem').read_text()
rows = re.findall(r'^0x[0-9a-f]+:\|(.*)\|$', mem, re.M)
cells = [c for row in rows for c in row.split('|')]
assert len(cells) == 256, 'Unexpected SDCC internal RAM report'
counts = collections.Counter(cells)
stack = re.search(r'Stack starts at: 0x([0-9a-f]+).*with (\d+) bytes available', mem)
flash = re.search(r'ROM/EPROM/FLASH\s+0x\w+\s+0x\w+\s+(\d+)\s+(\d+)', mem)
xram = re.search(r'EXTERNAL RAM\s+0x\w+\s+0x\w+\s+(\d+)\s+(\d+)', mem)
assert stack and flash and xram
modules = []
names = ['wire', 'pd', 'dp', 'ch543_phy', 'ch543_board',
         'main-source' if 'source' in p.stem else 'main-sink']
if p.parent.name == 'board2':
    names[4:5] = ['ch543_board2', 'ch9445', 'soft_i2c']
for name in names:
    rel = (p.parent / 'target' / (name + '.rel')).read_text()
    areas = collections.Counter()
    for area, size in re.findall(r'^A (\S+) size ([0-9A-F]+) ', rel, re.M):
        areas[area] += int(size, 16)
    modules.append(dict(module=name, code=areas['CSEG'] + areas['CONST'] + areas['HOME'],
                        data=areas['DSEG'], idata=areas['ISEG'], xdata=areas['XSEG'],
                        overlay=areas['OSEG'], bit_count=areas['BSEG']))
result = dict(image=p.name, sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
              flash=int(flash[1]), flash_limit=int(flash[2]),
              iram_occupied=256-counts['S']-counts[' '], iram_holes=counts[' '],
              stack_start=int(stack[1], 16), stack_capacity=int(stack[2]),
              xram=int(xram[1]), xram_limit=int(xram[2]), modules=modules)
assert result['flash'] <= 14336 and result['xram'] <= 256
assert result['stack_capacity'] >= 48, 'Stack allocation regressed; review generated ASM'
p.with_suffix('.resources.json').write_text(json.dumps(result, indent=2) + '\n')
table = ['| Module | code+const | data | idata | xdata | overlay* | bits |',
         '|---|---:|---:|---:|---:|---:|---:|']
for m in modules:
    table.append('| {module} | {code} | {data} | {idata} | {xdata} | {overlay} | {bit_count} |'.format(**m))
report = (f"{p.name}: Flash {result['flash']}/14336 B; "
          f"iRAM occupied {result['iram_occupied']} B + holes {result['iram_holes']} B; "
          f"stack 0x{result['stack_start']:02X}..0xFF = {result['stack_capacity']} B; "
          f"xRAM {result['xram']}/256 B.\n\n" + '\n'.join(table) +
          '\n\n* Overlay columns overlap and must not be summed. Register banks, '
          'bit backing bytes and linker/runtime overhead are included only in totals. '
          'Stack capacity is not measured usage; see docs/STACK.md.\n')
p.with_suffix('.resources.md').write_text(report)
with (p.parent / 'resource-history.jsonl').open('a') as f:
    f.write(json.dumps(result) + '\n')
print(report)
