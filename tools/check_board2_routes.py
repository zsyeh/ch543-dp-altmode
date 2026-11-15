#!/usr/bin/env python3
"""Check proposed CH9445 routes against the EPRO-derived connector netlist.

Source oracles: CH9445DS1 v1.5 Fig 5.2; ST TA0356 Tables 1-4;
TI SLLA365 Fig 9 for native DP Source versus Sink receptacle pin ordering.
High-speed coupling capacitors/0-ohm links are edges, not DC short assertions.
"""
import json
from pathlib import Path

parts = json.loads((Path(__file__).resolve().parents[1] / 'hardware/board2-netmap.json').read_text())['components']
parent = {}


def root(net):
    if parent.setdefault(net, net) != net:
        parent[net] = root(parent[net])
    return parent[net]


def pad(part, number):
    return root(parts[part]['pads'][str(number)])


for name, part in parts.items():
    if name.startswith('C'):
        nets = list(part['pads'].values())
        assert len(nets) == 2
        parent[root(nets[0])] = root(nets[1])

# Type-C physical P/N pads; no dependence on the PCB's RX1/RX2 net names.
typec = {'A11': ('A11', 'A10'), 'B11': ('B11', 'B10'),
         'A2': ('A2', 'A3'), 'B2': ('B2', 'B3')}
inputs = ((5, 6), (3, 4), (1, 2), (31, 32))  # CH9445 CRX2, CTX2, CTX1, CRX1
outputs = ((18, 17), (20, 19), (22, 21), (24, 23))
allowed = ({0, 3}, {1, 2}, {1, 2}, {0, 3})  # LNA/B/C/D switch matrix


def pair(part, pins):
    return tuple(pad(part, p) for p in pins)


def route(role, assignment, flipped):
    # Native DP sink ML0/1/2/3 vs native DP source ML0/1/2/3 P/N pins.
    native = ((12, 10), (9, 7), (6, 4), (3, 1)) if role == 1 else ((1, 3), (4, 6), (7, 9), (10, 12))
    if role == 1:
        targets = ['A11', 'B2', 'A2', 'B11']
    elif assignment == 'E':
        targets = ['B11', 'A2', 'B2', 'A11']
    else:
        targets = ['B2', 'A11', 'B11', 'A2']
    if assignment == 'D':
        targets = targets[:2]
    swap = {'A11': 'B11', 'B11': 'A11', 'A2': 'B2', 'B2': 'A2'}
    selectors = [0, 0, 0, 0]
    for lane, target in enumerate(targets):
        if flipped:
            target = swap[target]
        incoming = pair('PORT1', typec[target])
        outgoing = pair('USB2', native[lane])
        ins = [i for i, pins in enumerate(inputs) if pair('U11', pins) == incoming]
        outs = [o for o, pins in enumerate(outputs) if pair('U11', pins) == outgoing]
        if len(ins) != 1 or len(outs) != 1 or ins[0] not in allowed[outs[0]]:
            return None
        selectors[outs[0]] |= 1 << ins[0]
    if assignment == 'D':
        # ST's D route has USB TX on A2 and RX on B11, swapped when flipped.
        for target, out in [('A2', 3), ('B11', 1)]:
            incoming = pair('PORT1', typec[swap[target] if flipped else target])
            index = next(i for i, pins in enumerate(inputs) if pair('U11', pins) == incoming)
            selectors[out] |= 1 << index
    return selectors[0] | selectors[1] << 4, selectors[2] | selectors[3] << 4


expected = {(1, False): (0xa1, 0x40), (1, True): (0x58, 0x20),
            (2, False): (0x80, 0x52), (2, True): (0x10, 0xa4)}
for (role, flipped), registers in expected.items():
    assert route(role, 'D', flipped) == registers
    assert route(role, 'C', flipped) is None
    assert route(role, 'E', flipped) is None
assert pad('U10', 5) == pad('U11', 9)  # SDA
assert pad('U10', 6) == pad('U11', 8)  # SCL
assert parts['R7']['schematic_add_into_bom'] == 'no'
print('EPRO netlist: all 4 D routes derived; C/E not routable; I2C pin mapping and R7 BOM flag checked')
