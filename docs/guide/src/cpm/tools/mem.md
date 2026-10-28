# MEM

```
MEM
MEM T
```

`MEM` shows the CP/M version, the TPA (the room programs get, up to the
BDOS or an RSX below it), the machine's memory from the MMU, and what each
bank holds:

```
CP/M 3.1
TPA     0100H-F205H: 61702 bytes (60K)
Memory  160K: bank 0 64K, banks 1-2 48K each (C000H-FFFFH common)
Bank 0  the system: banked BDOS and BIOS, disk buffers
Bank 1  the TPA (programs)
Bank 2  not used by CP/M
```

`MEM T` then tests the banks CP/M does not use (all but bank 0 and the
TPA's; refused under MP/M), for instance the PSRAM on the PSRAM boards:
every free bank is filled with a pattern made from the address and the bank
number, then each is read back, and again with the complement, so every bit
is written both ways and two banks that land on the same memory show up as
errors. It changes those banks' contents, and runs with interrupts off
while another bank is selected (from C000H, in common memory). Under CP/M
2.2 on a PSRAM board:

```
Testing the free banks (two passes)...
Bank 1  ok
...
Bank 15  ok
```

A bank with errors shows how many bytes were wrong (`Bank 3  12 bytes
wrong`).
