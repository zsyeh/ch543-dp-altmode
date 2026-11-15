ch543-board2-dp-source-guarded.ihx: Flash 11038/14336 B; iRAM occupied 191 B + holes 1 B; stack 0xC0..0xFF = 64 B; xRAM 204/256 B.

| Module | code+const | data | idata | xdata | overlay* | bits |
|---|---:|---:|---:|---:|---:|---:|
| wire | 195 | 0 | 0 | 0 | 4 | 0 |
| pd | 3137 | 24 | 67 | 60 | 8 | 0 |
| dp | 3572 | 45 | 0 | 0 | 4 | 2 |
| ch543_phy | 2246 | 12 | 0 | 130 | 0 | 0 |
| ch543_board2 | 606 | 3 | 2 | 6 | 1 | 0 |
| ch9445 | 570 | 2 | 0 | 8 | 2 | 0 |
| soft_i2c | 431 | 2 | 0 | 0 | 1 | 3 |
| main-source | 189 | 0 | 0 | 0 | 0 | 0 |

* Overlay columns overlap and must not be summed. Register banks, bit backing bytes and linker/runtime overhead are included only in totals. Stack capacity is not measured usage; see docs/STACK.md.
