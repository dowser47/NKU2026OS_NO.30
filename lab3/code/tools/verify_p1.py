#!/usr/bin/env python3
"""Exercise the real S-mode trap path, without implementing P3 in the kernel."""
import argparse
import json
from pathlib import Path
import socket
import subprocess


GDB_SCRIPT = r'''
set pagination off
set confirm off
set remotetimeout 10
target remote 127.0.0.1:PORT
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
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--gdb", default="gdb-multiarch")
    parser.add_argument("--image", default="bin/best_fit/0/ucore.img")
    parser.add_argument("--kernel", default="bin/best_fit/0/kernel")
    args = parser.parse_args()
    evidence = Path("evidence/p1")
    evidence.mkdir(parents=True, exist_ok=True)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    script = evidence / "verify.gdb"
    script.write_text(GDB_SCRIPT.replace("PORT", str(port)))
    command = [args.qemu, "-machine", "virt", "-m", "128M", "-nographic",
               "-bios", "default", "-kernel", args.image, "-S",
               "-gdb", f"tcp:127.0.0.1:{port}"]
    environment = {"qemu": command, "gdb": args.gdb, "stage": "P1 only",
                   "source_base": "328fd3afd8036fb87a6790b3ee697a05e2100a46"}
    for name in (args.qemu, args.gdb, "riscv64-unknown-elf-gcc"):
        environment[name] = subprocess.check_output([name, "--version"], text=True).splitlines()[0]
    (evidence / "environment.json").write_text(json.dumps(environment, indent=2) + "\n")
    with (evidence / "qemu.log").open("w") as log:
        qemu = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            try:
                run = subprocess.run([args.gdb, "-q", "-nx", "-batch", args.kernel, "-x", str(script)],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=25)
            except subprocess.TimeoutExpired as error:
                output = error.stdout or b""
                if isinstance(output, bytes):
                    output = output.decode(errors="replace")
                (evidence / "gdb.log").write_text(output)
                (evidence / "summary.json").write_text('{"passed": false, "error": "GDB timeout"}\n')
                print(output)
                raise SystemExit("P1 verification timed out; see evidence/p1/gdb.log")
            (evidence / "gdb.log").write_text(run.stdout)
            passed = run.returncode == 0 and "P1 PASS:" in run.stdout
            (evidence / "summary.json").write_text(json.dumps({"passed": passed, "stage": "P1"}, indent=2) + "\n")
            if not passed:
                print(run.stdout)
                raise SystemExit(1)
            print(next(line for line in run.stdout.splitlines() if line.startswith("P1 PASS:")))
        finally:
            qemu.terminate()
            try:
                qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait()


if __name__ == "__main__":
    main()
