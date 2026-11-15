$NOMOD51
                NAME    ?C_STARTUP
                PUBLIC  ?C_STARTUP, CH543_RESET
                EXTRN   CODE (?C_START)

; Keil's C runtime initializes explicit C initializers after this reset path.
; CH543 has 256 B of iRAM and 256 B of on-chip xRAM.
?STACK          SEGMENT IDATA
?C_C51STARTUP   SEGMENT CODE

                RSEG    ?STACK
                DS      1

                CSEG    AT 0
?C_STARTUP:     LJMP    CH543_RESET

                RSEG    ?C_C51STARTUP
CH543_RESET:    MOV     0A8H,#0         ; IE
                MOV     0E8H,#0         ; IE_EX
                MOV     0A2H,#0         ; XBUS_AUX: DPTR0, no auto-increment
                MOV     0D0H,#0         ; PSW: use register bank 0
                CLR     A
                MOV     R0,#0FFH
CLEAR_IRAM:     MOV     @R0,A
                DJNZ    R0,CLEAR_IRAM
                ; R0 itself is iRAM address 0 and already holds zero.
                MOV     DPTR,#0
CLEAR_XRAM:     MOVX    @DPTR,A
                INC     DPTR
                DJNZ    R0,CLEAR_XRAM   ; zero counter means 256 iterations
                MOV     081H,#?STACK-1 ; SP, first push lands in ?STACK
                LJMP    ?C_START
                END
