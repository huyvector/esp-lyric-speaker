#!/usr/bin/env python3
"""Mac terminal flasher and interactive USB logs for dual ESP32 Vinyl."""
import argparse
import codecs
from datetime import datetime
import hashlib
from pathlib import Path
import select
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
ROLE = "audio"


def verify_images(folder):
    expected = {}
    for line in (folder / "SHA256SUMS.txt").read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        expected[name.strip()] = digest
    for name in ("factory.bin", "app.bin", "ota-data.bin"):
        data = (folder / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected.get(name):
            raise ValueError(f"Sai checksum: {name}. Tải/giải nén lại bộ firmware.")


def flash_arguments(port, baud, app_only, rom, major):
    def option(name):
        return "--" + (name.replace("_", "-") if major >= 5 else name)
    args = [sys.executable, "-u", "-m", "esptool", "--chip", "esp32" if ROLE == "audio" else "esp32c3", "--port", port, "--baud", str(baud)]
    if rom:
        args += ["--no-stub"]
    args += ["write-flash" if major >= 5 else "write_flash"]
    if rom:
        args += [option("no_compress")]
    args += [option("flash_mode"), "dio", option("flash_freq"), "40m", option("flash_size"), "4MB"]
    folder = ROOT / "firmware" / ROLE
    if app_only:
        args += ["0xe000", str(folder / "ota-data.bin"), "0x10000", str(folder / "app.bin")]
    else:
        args += ["0x0", str(folder / "factory.bin")]
    return args


class Logs:
    def __init__(self, kind):
        folder = ROOT / "logs" / ROLE
        folder.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().astimezone().strftime("%Y%m%d-%H%M%S-%f")
        self.path = folder / f"{kind}-{stamp}.log"
        self.latest = folder / f"{kind}-last.log"
        self.handles = [self.path.open("wb"), self.latest.open("wb")]
        self.write((f"Vinyl Dual | {datetime.now().astimezone().isoformat()}\n").encode())

    def write(self, data):
        for f in self.handles:
            f.write(data)
            f.flush()

    def close(self):
        for f in self.handles:
            f.close()


def choose_port(serial_tools, explicit):
    if explicit:
        return explicit
    ports = sorted({p.device for p in serial_tools.comports()
                    if p.vid is not None or any(s in p.device.lower() for s in ("usb", "slab", "wch"))})
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise ValueError("Chưa thấy cổng USB. Cắm ESP32 bằng cáp data, rồi chạy lại. Đóng Serial Monitor khác nếu đang mở.")
    print("Có nhiều cổng USB:")
    for i, port in enumerate(ports, 1):
        print(f"  {i}. {port}")
    if not sys.stdin.isatty():
        raise ValueError("Thêm --port /dev/cu.TEN_CONG để chọn đúng board.")
    answer = input("Chọn số cổng của ESP32: ").strip()
    if not answer.isdigit() or not 1 <= int(answer) <= len(ports):
        raise ValueError("Chưa chọn được cổng hợp lệ; chưa nạp gì.")
    return ports[int(answer) - 1]


def flash(command):
    log = Logs("flash")
    print(f"Log nạp: {log.path}", flush=True)
    log.write((shlex.join(command) + "\n").encode())
    try:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
            try:
                while True:
                    data = process.stdout.read1(4096)
                    if not data:
                        break
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                    log.write(data)
                code = process.wait()
            except KeyboardInterrupt:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                raise
        if code:
            raise RuntimeError(f"Nạp chưa thành công (mã {code}). Xem {log.latest}; thử --baud 115200 hoặc --rom.")
    finally:
        log.close()


def monitor(serial_module, port, reset, status_every):
    log = Logs("runtime")
    print(f"\nLog chạy: {log.path}\nBản mới nhất: {log.latest}", flush=True)
    print("Serial 115200. Gõ status / play / pause / next / prev / setup rồi Enter. Ctrl+C để thoát.", flush=True)
    link = serial_module.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1)
    link.dtr = False
    link.rts = False
    link.port = port
    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    try:
        for attempt in range(40):
            try:
                link.open()
                break
            except serial_module.SerialException:
                if attempt == 39: raise
                time.sleep(0.25)
        if reset:
            print("Reset board để thu log từ đầu…", flush=True)
            link.dtr = False
            link.rts = True
            time.sleep(0.1)
            link.rts = False
        next_status = time.monotonic() + 2
        stdin_open = True
        while True:
            data = link.read(min(link.in_waiting, 4096) or 1)
            if data:
                log.write(data)
                sys.stdout.write(decoder.decode(data))
                sys.stdout.flush()
            if stdin_open and select.select([sys.stdin], [], [], 0)[0]:
                command = sys.stdin.readline()
                if not command:
                    stdin_open = False
                elif command.strip():
                    command = command.strip()
                    if len(command.encode()) > 150:
                        print("Lệnh quá dài; chưa gửi.")
                        continue
                    link.write((command + "\n").encode())
                    log.write(("\n> " + command + "\n").encode())
            if status_every > 0 and time.monotonic() >= next_status:
                link.write(b"status\n")
                next_status = time.monotonic() + status_every
    except KeyboardInterrupt:
        print("\nĐã đóng monitor; loa vẫn chạy.", flush=True)
    finally:
        link.close()
        log.close()


def main(argv=None):
    global ROLE
    parser = argparse.ArgumentParser(description="Nạp Vinyl ESP32 trên Mac rồi đọc/lưu log USB.")
    parser.add_argument("--role", required=True, choices=("audio", "display"), help="audio: ESP32 thường; display: ESP32-C3")
    parser.add_argument("--port", help="Ví dụ /dev/cu.usbserial-XXXX")
    parser.add_argument("--baud", type=int, default=460800, help="Baud nạp; monitor luôn 115200")
    parser.add_argument("--app", action="store_true", help="Cập nhật app0 + đặt lại OTA selection, giữ NVS/cache; CHỈ dùng sau khi đã nạp Dual factory")
    parser.add_argument("--rom", action="store_true", help="Nạp không stub, không nén")
    parser.add_argument("--monitor-only", action="store_true", help="Chỉ đọc log, không ghi flash")
    parser.add_argument("--monitor", action="store_true", help="Mở log sau khi nạp (đã bật mặc định; tương thích lệnh dự án cũ)")
    parser.add_argument("--no-monitor", action="store_true", help="Nạp xong thì thoát")
    parser.add_argument("--reset", action="store_true", help="Reset khi mở monitor-only để lấy log boot")
    parser.add_argument("--status-every", type=float, default=15, help="Đọc trạng thái định kỳ, giây; 0 để tắt")
    parser.add_argument("--dry-run", action="store_true", help="Kiểm tra checksum/tham số; không mở cổng hoặc ghi flash")
    args = parser.parse_args(argv)
    ROLE = args.role
    if args.baud <= 0 or args.status_every < 0:
        parser.error("Baud phải >0 và chu kỳ status phải >=0.")
    if not args.monitor_only:
        verify_images(ROOT / "firmware" / ROLE)
        print(f"Checksum OK. Target: {ROLE} / {'ESP32' if ROLE == 'audio' else 'ESP32-C3'} / 4 MB / Dual OTA partitions.", flush=True)
    if args.dry_run:
        print("Monitor only; không nạp." if args.monitor_only else ("App: app0 0x10000 + OTA selection 0xe000, giữ NVS/cache." if args.app else "Factory: 0x0, cấu hình Wi-Fi/Bluetooth sẽ khởi tạo lại."))
        print("Dry run OK. Chưa mở USB, chưa ghi flash.")
        return 0
    try:
        import esptool
        import serial
        from serial.tools import list_ports
    except ImportError as error:
        raise RuntimeError("Thiếu esptool/pyserial. Chạy bash flash.command để tự chuẩn bị môi trường.") from error
    major = int(esptool.__version__.split(".")[0])
    if major not in (4, 5):
        raise RuntimeError("Script hỗ trợ esptool 4.x/5.x. Dùng bash flash.command.")
    port = choose_port(list_ports, args.port)
    print(f"Cổng: {port} | esptool {esptool.__version__}", flush=True)
    if not args.monitor_only:
        print("Cập nhật app, giữ cấu hình." if args.app else "Nạp factory lần đầu: cấu hình Wi-Fi/Bluetooth sẽ khởi tạo lại.", flush=True)
        flash(flash_arguments(port, args.baud, args.app, args.rom, major))
        print("\nNạp thành công.", flush=True)
    if args.monitor_only or args.monitor or not args.no_monitor:
        monitor(serial, port, reset=args.reset or (not args.monitor_only and ROLE == "audio"), status_every=args.status_every)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit("\nĐã dừng nạp. Nếu đang ghi dở, cần chạy nạp lại.")
    except (ValueError, OSError, RuntimeError) as error:
        sys.exit(str(error))
