"""Decode the hw_survey struct (hw_survey.c) from hex bytes on stdin."""
import struct, sys
b=bytes(int(h,16) for h in sys.stdin.read().split())
print(f"magic {struct.unpack_from('<I',b,0)[0]:08x} (53555256 = done), step {struct.unpack_from('<I',b,4)[0]}")
names=["idle (radio READY, GPS on)","TX carrier PA 0x00","TX carrier PA 0x20","TX carrier PA 0x40","TX carrier PA 0x7F","radio SLEEP (MCU on HSI)","radio SHUTDOWN (SDN)","radio re-init, MCU on radio clk","GPS software backup","GPS woken"]
print("state                              PA0 raw  PB1 raw  Vref  VDDA   PA0 V   PB1 V  temp C")
for i,n in enumerate(names):
    pa0,pb1,vr,tp=struct.unpack_from('<4H',b,8+8*i)
    vdda=1.20*4095/vr if vr else 0
    v=lambda r: r*vdda/4095
    t=(1.41-v(tp))/0.0043+25
    print(f"{n:34s} {pa0:6d} {pb1:8d} {vr:5d}  {vdda:4.2f}  {v(pa0):6.3f}  {v(pb1):6.4f}  {t:5.1f}")
pr,ps,pd=struct.unpack_from('<3H',b,88)
print(f"PD0 (GPIO2 DIV_CLK) transitions /10000: READY {pr}, SLEEP {ps}, SHUTDOWN {pd}")
lse,ppsb,wake=b[94],b[95],b[96]; hdr=b[97:101]; back,idle,pc13=b[101],b[102],b[103]
print(f"LSE 32k crystal ready: {lse}; PPS lock 3 s into GPS backup: {ppsb}; GPS wake poll ok: {wake}, reply header {hdr.hex(' ')}; radio clock back: {back}")
print(f"I2C idle levels SCL={idle&1} SDA={idle>>1&1}; PC13 (EEPROM busy) = {pc13}")
ack=b[104:120]; print("I2C ACKs:", [hex(a) for a in range(128) if ack[a>>3]>>(a&7)&1])
print(f"EEPROM user read ok {b[120]}, system read ok {b[121]}")
u=b[122:634]
for off in range(0,512,32):
    row=u[off:off+32]; print(f"  user {off:03x}: {row.hex(' ')}  |{''.join(chr(c) if 32<=c<127 else '.' for c in row)}|")
print("  sys 0000:", b[634:650].hex(' '))
print("  sys 0900:", b[650:682].hex(' '))
print(f"EEPROM write test at 0x1FC: ACK {b[682]}, ready after {b[683]} ms, before {b[684:688].hex(' ')}, written {b[688:692].hex(' ')}, restored {b[692:696].hex(' ')}")
for name,off in [("floor, MCU 24 MHz",696),("floor, MCU 8 MHz",704)]:
    pa0,pb1,vr,tp=struct.unpack_from('<4H',b,off); vdda=1.20*4095/vr if vr else 0
    print(f"{name} (LEDs off, radio shutdown, GPS backup): PA0 {pa0*vdda/4095:.3f} V, PB1 {pb1*vdda/4095:.4f} V")
