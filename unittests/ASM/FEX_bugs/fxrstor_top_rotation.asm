%ifdef CONFIG
{
  "RegData": {
    "RAX": "0x0",
    "RBX": "0x3ff0000000000000",
    "RCX": "0x0"
  }
}
%endif

; ST0/ST1 save slots are logical, not physical. Nonzero TOP must be
; restored before converting them back into CPUState's physical slots.
fninit
fld1
fldz
fxsave [rel .saved]
fninit
fldpi
fxrstor [rel .saved]
fstp qword [rel .zero]
fstp qword [rel .one]
mov rax, [rel .zero]
mov rbx, [rel .one]
fnstsw ax
movzx ecx, ax
and ecx, 0x3800
mov rax, [rel .zero]
hlt

align 16
.saved: times 64 dq 0
.zero: dq 0
.one: dq 0
