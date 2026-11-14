ch543-dp-sink.ihx: Flash 9913/14336 B; iRAM occupied 187 B + holes 3 B; stack 0xBE..0xFF = 66 B; xRAM 190/256 B.

| Module | code+const | data | idata | xdata | overlay* | bits |
|---|---:|---:|---:|---:|---:|---:|
| wire | 195 | 0 | 0 | 0 | 4 | 0 |
| pd | 3123 | 24 | 67 | 60 | 8 | 0 |
| dp | 3443 | 45 | 0 | 0 | 4 | 2 |
| ch543_phy | 2246 | 12 | 0 | 130 | 0 | 0 |
| ch543_board | 625 | 5 | 0 | 0 | 2 | 0 |
| main-sink | 189 | 0 | 0 | 0 | 0 | 0 |

* Overlay columns overlap and must not be summed. Register banks, bit backing bytes and linker/runtime overhead are included only in totals. Stack capacity is not measured usage; see docs/STACK.md.
