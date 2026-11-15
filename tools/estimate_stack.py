#!/usr/bin/env python3
"""Conservative CFG stack estimate for this SDCC assembly subset, not a proof.

Walks both outcomes of conditional branches, follows direct calls/tail calls,
includes PUSH/POP and 2-byte return addresses. Fails on unresolved/indirect
calls, recursion or unbalanced paths; C runtime startup is outside this scope.
Input is generated .asm, so review again if the toolchain/ISR model changes.
"""
import argparse
from functools import lru_cache
import json
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('objects', type=Path)
p.add_argument('--image', type=Path)
p.add_argument('--no-interrupts', action='store_true', help='Standalone diagnostic only; IE/IE_EX remain zero')
args = p.parse_args()
functions = {}
for path in sorted(args.objects.glob('*.asm')):
    if path.stem == 'main-sink':
        continue  # identical functions; role constant only
    code, current = False, None
    for raw in path.read_text().splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.startswith('.area'):
            code = bool(re.match(r'\.area\s+CSEG\b', line))
            current = None
            continue
        if not code or not line or line.startswith('.') or '=' in line:
            continue
        if re.fullmatch(r'_\w+:', line):
            name = line[:-1]
            if name in functions:
                raise ValueError('duplicate function ' + name)
            current = functions[name] = {'labels': {}, 'ops': []}
        elif current is not None and line.endswith(':'):
            current['labels'][line[:-1]] = len(current['ops'])
        elif current is not None:
            parts = line.split(None, 1)
            current['ops'].append((parts[0], parts[1] if len(parts) > 1 else ''))
active = set()


@lru_cache(None)
def peak(name):
    if name not in functions or name in active:
        raise ValueError('unresolved or recursive call: ' + name)
    active.add(name)
    f = functions[name]
    seen, todo, best, path = {}, [(0, 0)], 0, name
    while todo:
        pc, depth = todo.pop()
        if pc in seen:
            if seen[pc] != depth:
                raise ValueError(f'{name}:{pc}: inconsistent stack depth')
            continue
        seen[pc] = depth
        if pc >= len(f['ops']):
            raise ValueError(name + ': fallthrough beyond function')
        op, arg = f['ops'][pc]
        if re.search(r'\bsp\b', arg):
            raise ValueError(name + ': explicit SP access needs manual audit')
        if op == 'push':
            depth += 1
        elif op == 'pop':
            depth -= 1
            if depth < 0:
                raise ValueError(name + ': stack underflow')
        if depth > best:
            best, path = depth, name + ':push'
        if op in ('ret', 'reti'):
            if depth:
                raise ValueError(name + ': nonzero stack at return')
            continue
        if op == 'jmp':
            raise ValueError(name + ': indirect jump needs manual audit')
        if op in ('lcall', 'acall') or (op in ('ljmp', 'ajmp') and arg.startswith('_')):
            extra, subpath = peak(arg)
            total = depth + extra + (2 if op.endswith('call') else 0)
            if total > best:
                best, path = total, f'{name}[saved={depth}] -> {subpath}'
            if op.endswith('jmp'):
                if depth:
                    raise ValueError(name + ': tail call with pending stack')
                continue
        elif op in ('sjmp', 'ljmp', 'ajmp', 'jz', 'jnz', 'jc', 'jnc', 'jb', 'jnb', 'jbc', 'djnz', 'cjne'):
            target = arg.split(',')[-1].strip()
            if target not in f['labels']:
                raise ValueError(name + ': unresolved branch ' + target)
            todo.append((f['labels'][target], depth))
            if op in ('sjmp', 'ljmp', 'ajmp'):
                continue
        todo.append((pc + 1, depth))
    active.remove(name)
    return best, path


for function in functions:
    peak(function)
foreground, chain = peak('_main')
if args.no_interrupts:
    assert not any(n.endswith('_isr') for n in functions), 'unexpected ISR in diagnostic'
    low = high = 0
else:
    low = max(peak(n)[0] + 2 for n in functions if n.endswith('_isr') and n != '_pd_isr')
    high = peak('_pd_isr')[0] + 2
result = {'functions': len(functions), 'foreground': foreground,
                  'foreground_path': chain, 'low_isr_with_return': low,
                  'pd_isr_with_return': high, 'combined_estimate': foreground + low + high,
                  'scope': 'static generated ASM; excludes C runtime startup; not hardware watermarked'}
if args.image:
    resource = json.loads(args.image.with_suffix('.resources.json').read_text())
    result['capacity'] = resource['stack_capacity']
    result['estimated_headroom'] = result['capacity'] - result['combined_estimate']
    assert result['estimated_headroom'] >= 16, 'review stack headroom before release'
    args.image.with_suffix('.stack.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
