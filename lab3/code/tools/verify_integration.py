#!/usr/bin/env python3
"""在临时源码副本中验证真实异常、PMM 中断临界区及正常关机，不修改生产镜像。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile

GDB_SCRIPT = r'''
set pagination off
set confirm off
set remotetimeout 10
target remote 127.0.0.1:PORT
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
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--best-fit", action="store_true", help="验证 Best-Fit，而非框架默认 First-Fit")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent.parent
    evidence = source / "evidence/baseline-integration" / ("best-fit" if args.best_fit else "supplemental")
    evidence.mkdir(parents=True, exist_ok=True)
    summary = {"passed": False, "pmm": "best_fit" if args.best_fit else "first_fit"}
    qemu = None
    try:
        with tempfile.TemporaryDirectory(prefix="lab3-integration-") as temp:
            work = Path(temp) / "code"
            shutil.copytree(source, work, ignore=shutil.ignore_patterns("bin", "obj", "evidence", "__pycache__", ".git"))
            # 只在临时副本中接入补充测试；原始课程测试完全独立。
            shutil.copy2(work / "tools/test_integration.c", work / "kern/driver/test_integration.c")
            init = work / "kern/init/init.c"
            text = init.read_text().replace("int kern_init(void) __attribute__", "void check_lab3_integration(void);\n\nint kern_init(void) __attribute__")
            text = text.replace("    clock_init();", "    check_lab3_integration();\n    clock_init();")
            init.write_text(text)
            build = ["make", "-j4"]
            if args.best_fit:
                build.append("DEFS+=-DPMM_USE_BEST_FIT -Ducore_test")
            with (evidence / "build.log").open("w") as log:
                subprocess.run(build, cwd=work, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            gdb_script = evidence / "verify.gdb"
            gdb_script.write_text(GDB_SCRIPT.replace("PORT", str(port)))
            qemu_command = ["qemu-system-riscv64", "-machine", "virt", "-m", "128M", "-nographic", "-bios", "default",
                            "-kernel", str(work / "bin/ucore.img"), "-monitor", "none", "-S", "-gdb", f"tcp:127.0.0.1:{port}"]
            with (evidence / "qemu.log").open("w") as output:
                qemu = subprocess.Popen(qemu_command, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT)
                with (evidence / "gdb.log").open("w") as log:
                    subprocess.run(["gdb-multiarch", "-q", "-nx", "-batch", str(work / "bin/kernel"), "-x", str(gdb_script)],
                                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=40)
                qemu.wait(timeout=5)
            output = (evidence / "qemu.log").read_text()
            gdb_output = (evidence / "gdb.log").read_text()
            assert "INTEGRATION GDB PASS:" in gdb_output
            assert "[Integration] PMM interrupt guards: PASS" in output
            assert "[Integration] exception boundaries: PASS (8 length + 15 fallback)" in output
            assert "check_alloc_page() succeeded!" in output
            assert len(re.findall(r"(?m)^100 ticks$", output)) == 10
            assert qemu.returncode == 0
            assert not re.search(r"(?i)panic|assertion failed", output)
            if args.best_fit:
                assert "grading: 6 / 6 points" in output
            summary.update(passed=True, real_exception_cases=4, gpr_restored=32, host_tests=False,
                           length_cases=8, fallback_cases=15, pmm_guard_states=["enabled", "disabled", "nested", "allocation failure"],
                           timer_ticks_at_shutdown=1000, tick_lines=10, qemu_exit=0,
                           trap_sha256=hashlib.sha256((source / "kern/trap/trap.c").read_bytes()).hexdigest())
            print("INTEGRATION PASS: " + summary["pmm"] + ", PMM guards, 4 real exceptions, 8 length/15 fallback cases, 1000 ticks and shutdown")
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        summary["error"] = str(error)
        print("INTEGRATION FAIL: " + str(error))
    finally:
        if qemu is not None and qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait()
        (evidence / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    if not summary["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
