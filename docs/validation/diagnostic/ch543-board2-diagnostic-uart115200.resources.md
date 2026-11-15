ch543-board2-diagnostic-uart115200.ihx: Flash 1305/14336 B; iRAM occupied 18 B + holes 15 B; stack 0x21..0xFF = 223 B; xRAM 16/256 B.

| Module | code+const | data | idata | xdata | overlay* | bits |
|---|---:|---:|---:|---:|---:|---:|
| main | 97 | 0 | 0 | 0 | 0 | 0 |
| board2_diag | 685 | 5 | 1 | 16 | 0 | 0 |
| soft_i2c | 431 | 2 | 0 | 0 | 1 | 3 |

* Overlay columns overlap and must not be summed. Register banks, bit backing bytes and linker/runtime overhead are included only in totals. Stack capacity is not measured usage; see docs/STACK.md.
