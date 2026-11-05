
set pagination off
set confirm off
set remotetimeout 10
target remote 127.0.0.1:47220
hbreak idt_init
continue
finish
python
import gdb
import json
inf = gdb.selected_inferior()
def reg(name):
    return int(gdb.parse_and_eval("$" + name)) & ((1 << 64) - 1)
def check(ok, message):
    if not ok:
        raise gdb.GdbError(message)
def step():
    gdb.execute("stepi", to_string=True)
def at(symbol):
    return int(gdb.parse_and_eval("(unsigned long)&" + symbol))
gdb.execute("delete breakpoints")
check(at("__alltraps") % 4 == 0, "stvec entry alignment")
check(reg("stvec") == at("__alltraps"), "idt_init stvec")
check(reg("sscratch") == 0, "idt_init sscratch")
check(int(gdb.parse_and_eval("sizeof(struct trapframe)")) == 36 * 8, "frame size")
results = []
for length, opcode in [(4, b'\x73\x00\x10\x00'), (2, b'\x02\x90')]:
    print("CASE", length, "start", hex(reg("pc")), flush=True)
    site = reg("pc")
    original = bytes(inf.read_memory(site, length))
    inf.write_memory(site, opcode)
    # Distinct values make swapped slots and lost caller-saved registers visible.
    for i in range(1, 32):
        if i != 2:
            gdb.execute("set $x%d = %d" % (i, 0x12340000 + length * 0x100 + i))
    before = [reg("x%d" % i) for i in range(32)]
    check(before[2] % 16 == 0, "ABI stack alignment")
    entry_bp = gdb.Breakpoint("*__alltraps", type=gdb.BP_HARDWARE_BREAKPOINT)
    gdb.execute("continue", to_string=True)
    print("ENTRY", hex(reg("pc")), flush=True)
    entry_status = reg("sstatus")
    check(reg("sepc") == site and reg("scause") == 3, "hardware breakpoint exception")
    entry_bp.delete()
    trap_bp = gdb.Breakpoint("*trap", type=gdb.BP_HARDWARE_BREAKPOINT)
    gdb.execute("continue", to_string=True)
    print("TRAP", hex(reg("pc")), flush=True)
    frame = reg("a0")
    check(frame == before[2] - 36 * 8, "a0 must point to original-sp minus frame size")
    check(frame == reg("sp"), "call must preserve frame stack pointer")
    words = [int.from_bytes(inf.read_memory(frame + 8*i, 8), "little") for i in range(36)]
    check(words[:32] == before, "SAVE_ALL general registers mismatch")
    check(words[32] == entry_status and words[33] == site and words[35] == 3, "saved CSRs")
    check(words[34] == reg("stval"), "saved stval")
    check(reg("sscratch") == 0, "sscratch cleared after SAVE_ALL")
    # Simulate a future handler changing epc; production exception_handler stays empty.
    inf.write_memory(frame + 33 * 8, (site + length).to_bytes(8, "little"))
    restored_status = entry_status ^ 0x20  # Exercise loading SPIE from the frame.
    inf.write_memory(frame + 32 * 8, restored_status.to_bytes(8, "little"))
    trap_bp.delete()
    ret_bp = gdb.Breakpoint("*__trapret", type=gdb.BP_HARDWARE_BREAKPOINT)
    gdb.execute("continue", to_string=True)
    print("RESTORE", hex(reg("pc")), flush=True)
    check(reg("sp") == frame, "trap() return changed stack pointer")
    ret_bp.delete()
    for i in range(1, 32):
        if i != 2:
            gdb.execute("set $x%d = %d" % (i, 0x56780000 + i))
    # Stop immediately before sret, then verify its architectural effects separately.
    for _ in range(50):
        if bytes(inf.read_memory(reg("pc"), 4)) == b'\x73\x00\x20\x10':
            break
        step()
    else:
        raise gdb.GdbError("RESTORE_ALL did not reach sret")
    check([reg("x%d" % i) for i in range(32)] == before, "RESTORE_ALL lost registers or sp")
    check(reg("sepc") == site + length, "RESTORE_ALL used stale epc")
    check(reg("sstatus") == restored_status, "RESTORE_ALL used stale sstatus")
    # GDB's RISC-V software step does not reliably decode sret. Break at its
    # architectural destination instead, before any resumed instruction runs.
    resumed_bp = gdb.Breakpoint("*%d" % (site + length), type=gdb.BP_HARDWARE_BREAKPOINT)
    gdb.execute("continue", to_string=True)
    resumed_bp.delete()
    check(reg("pc") == site + length, "sret did not use updated epc")
    expected_status = (restored_status & ~(0x100 | 0x20 | 2)) | 0x20 | ((restored_status >> 4) & 2)
    check(reg("sstatus") == expected_status, "sret status semantics")
    check([reg("x%d" % i) for i in range(32)] == before, "sret changed GPRs")
    inf.write_memory(site, original)
    results.append({"instruction_bytes": length, "saved_gprs": 32, "restored_gprs": 32,
                    "frame_bytes": 288, "updated_epc_used": True, "status_restored": True})
print("P1_RESULTS=" + json.dumps(results, sort_keys=True))
print("P1 PASS: real 32-bit ebreak and 16-bit c.ebreak; frame, GPRs, CSRs, sp, sret")
end
disconnect
quit
