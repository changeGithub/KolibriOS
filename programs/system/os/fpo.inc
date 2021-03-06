; Magic for proc32.inc: prologue/epilogue for esp-based addressing
; fpo stands from Frame Pointer Omission
macro fpo_prologue procname,flag,parmbytes,localbytes,reglist
{
local loc,regcount
    loc = (localbytes+3) and (not 3)
    regcount = 0
    irps reg, reglist
    \{
        regcount = regcount+1
        push    reg
    \}
    parmbase@proc equ esp+4+regcount*4
    localbase@proc equ esp-localbytes
}
macro fpo_epilogue procname,flag,parmbytes,localbytes,reglist
{
    irps reg, reglist \{ reverse pop reg \}
    if flag and 10000b
        retn
    else
        retn    parmbytes
    end if
}
