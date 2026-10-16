#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""Ghi CPU/RAM CaroServer trong luc chay performance_probe.py (Windows).

Khong can pip/cai them thu vien. Dat file nay CUNG thu muc voi performance_probe.py.
Vi du (tai thu muc goc CaroOnline):
    py -u Extra\tests\monitor_caro.py --clients 50 --requests 500

Ket qua duoc luu vao Extra\results\monitor_*.csv va monitor_*.txt.
CPU% la ty le su dung CPU TOAN MAY (chia cho so logical CPU).
RAM la Working Set cua tien trinh, khong phai Private Working Set.
"""

import argparse
import csv
import ctypes
from ctypes import wintypes
from datetime import datetime
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import threading
import time


class FILETIME(ctypes.Structure):
    _fields_ = [("dwLowDateTime", wintypes.DWORD), ("dwHighDateTime", wintypes.DWORD)]


class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("PageFaultCount", wintypes.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]


def find_server_pid():
    result = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq CaroServer.exe", "/FO", "CSV", "/NH"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
    )
    output = result.stdout.decode("mbcs", errors="replace")
    for row in csv.reader(output.splitlines()):
        if len(row) >= 2 and row[0].strip().lower() == "caroserver.exe":
            return int(row[1].strip())
    raise RuntimeError("Khong tim thay CaroServer.exe. Hay mo Server truoc.")


class ResourceReader:
    def __init__(self, pid):
        self.pid = pid
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.psapi = ctypes.WinDLL("psapi", use_last_error=True)
        self.kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.GetProcessTimes.argtypes = (
            wintypes.HANDLE, ctypes.POINTER(FILETIME), ctypes.POINTER(FILETIME),
            ctypes.POINTER(FILETIME), ctypes.POINTER(FILETIME),
        )
        self.kernel.GetProcessTimes.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        self.psapi.GetProcessMemoryInfo.argtypes = (
            wintypes.HANDLE, ctypes.POINTER(PROCESS_MEMORY_COUNTERS), wintypes.DWORD,
        )
        self.psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
        # PROCESS_QUERY_LIMITED_INFORMATION + PROCESS_VM_READ
        self.handle = self.kernel.OpenProcess(0x1000 | 0x0010, False, pid)
        if not self.handle:
            raise OSError(ctypes.get_last_error(), "Khong the doc tien trinh Server")
        self.last_time = None
        self.last_cpu = None
        self.cpu_count = max(1, os.cpu_count() or 1)

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    @staticmethod
    def filetime_seconds(value):
        return ((value.dwHighDateTime << 32) | value.dwLowDateTime) / 10000000.0

    def read(self):
        created, exited, kernel_time, user_time = FILETIME(), FILETIME(), FILETIME(), FILETIME()
        if not self.kernel.GetProcessTimes(self.handle, ctypes.byref(created), ctypes.byref(exited),
                                           ctypes.byref(kernel_time), ctypes.byref(user_time)):
            raise OSError(ctypes.get_last_error(), "Khong doc duoc CPU cua Server")
        memory = PROCESS_MEMORY_COUNTERS()
        memory.cb = ctypes.sizeof(memory)
        if not self.psapi.GetProcessMemoryInfo(self.handle, ctypes.byref(memory), memory.cb):
            raise OSError(ctypes.get_last_error(), "Khong doc duoc RAM cua Server")
        now = time.perf_counter()
        cpu_seconds = self.filetime_seconds(kernel_time) + self.filetime_seconds(user_time)
        percent = None
        if self.last_time is not None and now > self.last_time:
            percent = max(0.0, 100.0 * (cpu_seconds - self.last_cpu) /
                          (now - self.last_time) / self.cpu_count)
        self.last_time, self.last_cpu = now, cpu_seconds
        return percent, memory.WorkingSetSize / 1048576.0


def main():
    parser = argparse.ArgumentParser(description="Auto measure CaroServer CPU/RAM + test latency")
    parser.add_argument("--clients", type=int, default=50)
    parser.add_argument("--requests", type=int, default=500)
    parser.add_argument("--interval", type=float, default=0.2,
                        help="Ghi mau moi N giay (mac dinh 0.2)")
    args = parser.parse_args()
    if not 1 <= args.clients <= 100 or not 1 <= args.requests <= 5000 or not 0.05 <= args.interval <= 5:
        parser.error("clients=1..100, requests=1..5000, interval=0.05..5")
    if os.name != "nt":
        parser.error("Chi ho tro Windows (vi du Windows 11)")

    root = Path(__file__).resolve().parent
    probe_path = root / "performance_probe.py"
    if not probe_path.is_file():
        parser.error("Khong co performance_probe.py cung thu muc voi monitor_caro.py")
    try:
        pid = find_server_pid()
        reader = ResourceReader(pid)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"LOI: {error}", file=sys.stderr, flush=True)
        return 1

    output_dir = root.parent / "results"
    output_dir.mkdir(parents=True, exist_ok=True)
    basename = f"monitor_{args.clients}clients_{args.requests}requests_{datetime.now():%Y%m%d_%H%M%S}"
    csv_path = output_dir / (basename + ".csv")
    txt_path = output_dir / (basename + ".txt")
    cmd = [sys.executable, "-u", str(probe_path), "--clients", str(args.clients),
           "--requests", str(args.requests)]
    print(f"Dang do CaroServer.exe PID={pid}; moi {args.interval:g} giay ghi CPU/RAM.", flush=True)
    print("Tu dong chay performance_probe.py. Khong can tu mo CMD thu hai.", flush=True)

    rows = []
    captured = []
    sample_started = time.perf_counter()
    test = None
    status = 1
    try:
        # Baseline measurement before launching load.
        reader.read()
        test = subprocess.Popen(cmd, cwd=str(root), stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True,
                                errors="replace", bufsize=1)

        def consume_output():
            assert test and test.stdout
            for line in test.stdout:
                print(line, end="", flush=True)
                captured.append(line)

        printer = threading.Thread(target=consume_output, daemon=True)
        printer.start()
        while test.poll() is None:
            cpu, ram = reader.read()
            if cpu is not None:
                rows.append({
                    "timestamp": datetime.now().astimezone().isoformat(timespec="milliseconds"),
                    "elapsed_s": round(time.perf_counter() - sample_started, 3),
                    "cpu_percent_total": round(cpu, 3),
                    "ram_working_set_mb": round(ram, 3),
                })
            time.sleep(args.interval)
        # Last observation, and finish collecting test output.
        cpu, ram = reader.read()
        if cpu is not None:
            rows.append({
                "timestamp": datetime.now().astimezone().isoformat(timespec="milliseconds"),
                "elapsed_s": round(time.perf_counter() - sample_started, 3),
                "cpu_percent_total": round(cpu, 3),
                "ram_working_set_mb": round(ram, 3),
            })
        printer.join(timeout=5)
        status = test.returncode or 0
    except KeyboardInterrupt:
        print("\nDa dung do bang Ctrl+C; van se luu du lieu thu thap duoc.", flush=True)
        if test and test.poll() is None:
            test.terminate()
            try:
                test.wait(timeout=3)
            except subprocess.TimeoutExpired:
                test.kill()
        status = 130
    finally:
        reader.close()

    with csv_path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=[
            "timestamp", "elapsed_s", "cpu_percent_total", "ram_working_set_mb",
        ])
        writer.writeheader()
        writer.writerows(rows)

    cpu_values = [row["cpu_percent_total"] for row in rows]
    ram_values = [row["ram_working_set_mb"] for row in rows]
    summary = [
        "CaroOnline - KET QUA DO TAI NGUYEN SERVER",
        f"Thoi gian: {datetime.now().astimezone().isoformat(timespec='seconds')}",
        f"He dieu hanh: {platform.platform()}",
        f"So CPU logic: {os.cpu_count()}",
        f"Server PID: {pid}",
        f"Client gia lap: {args.clients}; Yeu cau list_rooms: {args.requests}",
        f"So mau CPU/RAM: {len(rows)}; chu ky lay mau: {args.interval} giay",
        "CPU% la phan tram tren TONG CPU may, RAM la Working Set (khac Private Working Set).",
        "Ket qua chi do list_rooms qua 127.0.0.1, khong phai stress test toan bo gameplay.",
        f"Probe exit code: {status}",
    ]
    if rows:
        summary += [
            f"CPU trung binh (theo mau): {statistics.mean(cpu_values):.2f}%",
            f"CPU cao nhat (theo mau): {max(cpu_values):.2f}%",
            f"RAM trung binh (Working Set): {statistics.mean(ram_values):.2f} MB",
            f"RAM cao nhat (Working Set): {max(ram_values):.2f} MB",
        ]
    else:
        summary.append("Khong du mau de ket luan CPU/RAM.")
    summary += ["", "LOG KIEM THU:", "".join(captured).rstrip()]
    txt_path.write_text("\n".join(summary) + "\n", encoding="utf-8-sig")
    print("\n" + "\n".join(summary[:summary.index("LOG KIEM THU:")]), flush=True)
    print(f"\nDA LUU CSV: {csv_path}", flush=True)
    print(f"DA LUU TOM TAT: {txt_path}", flush=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
