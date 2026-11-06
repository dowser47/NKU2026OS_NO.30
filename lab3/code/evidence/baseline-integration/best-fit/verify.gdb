
set pagination off
set confirm off
set remotetimeout 10
target remote 127.0.0.1:56996
hbreak idt_init
continue
finish
python
import gdb, json
inf = gdb.selected_inferior()
def reg(name):
    return int(gdb.parse_and_eval("$" + name)) & ((1 << 64) - 1)
def check(ok, message):
    if not ok:
        raise gdb.GdbError(message)
def breakpoint(symbol):
    return gdb.Breakpoint(symbol, type=gdb.BP_HARDWARE_BREAKPOINT)
gdb.execute("delete breakpoints")
site = reg("pc")
boot_regs = [reg("x%d" % i) for i in range(32)]
boot_status = reg("sstatus")
results = []
cases = [("illegal16", b'\x00\x00', 2),
         ("illegal32", b'\x73\x10\x00\xc0', 2),
         ("breakpoint16", b'\x02\x90', 3),
         ("breakpoint32", b'\x73\x00\x10\x00', 3)]
for name, opcode, cause in cases:
    original = bytes(inf.read_memory(site, len(opcode)))
    inf.write_memory(site, opcode)
    gdb.execute("set $pc = %d" % site)
    for i in range(1, 32):
        if i != 2:
            gdb.execute("set $x%d = %d" % (i, 0x2412267000 + i))
    expected_regs = [reg("x%d" % i) for i in range(32)]
    entry = breakpoint("*trap")
    gdb.execute("continue", to_string=True)
    entry.delete()
    frame = reg("a0")
    before = bytes(inf.read_memory(frame, 36 * 8))
    words = [int.from_bytes(before[8*i:8*i+8], "little") for i in range(36)]
    check(words[:32] == expected_regs, name + " SAVE_ALL registers")
    check(words[33] == site and words[35] == cause, name + " real hardware cause/epc")
    ret = breakpoint("*__trapret")
    gdb.execute("continue", to_string=True)
    ret.delete()
    after = bytes(inf.read_memory(frame, 36 * 8))
    expected = bytearray(before)
    expected[33*8:34*8] = (site + len(opcode)).to_bytes(8, "little")
    check(after == bytes(expected), name + " must change only epc by instruction length")
    resume = breakpoint("*%d" % (site + len(opcode)))
    gdb.execute("continue", to_string=True)
    resume.delete()
    check(reg("pc") == site + len(opcode), name + " sret return address")
    check([reg("x%d" % i) for i in range(32)] == expected_regs, name + " restored registers")
    inf.write_memory(site, original)
    results.append({"case": name, "cause": cause, "epc_increment": len(opcode),
                    "only_epc_changed": True, "returned": True})
    print("INTEGRATION CASE PASS: " + name, flush=True)
# Restore the original boot context; now run actual timer interrupts after exceptions.
for i in range(1, 32):
    gdb.execute("set $x%d = %d" % (i, boot_regs[i]))
gdb.execute("set $pc = %d" % site)
gdb.execute("set $sstatus = %d" % boot_status)
shutdown = breakpoint("*sbi_shutdown")
gdb.execute("continue", to_string=True)
check(int(gdb.parse_and_eval("ticks")) == 1000, "shutdown must occur at tick 1000")
shutdown.delete()
print("INTEGRATION_RESULTS=" + json.dumps(results, sort_keys=True), flush=True)
print("INTEGRATION GDB PASS: four real exception returns, then 1000 timer interrupts", flush=True)
end
detach
quit
