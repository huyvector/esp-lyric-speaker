#!/bin/bash
set -euo pipefail
project="$(cd "$(dirname "$0")/.." && pwd)"
command -v pio >/dev/null || { echo "Cần PlatformIO Core (pio)."; exit 1; }
pio run -d "$project/audio"
pio run -d "$project/display"
echo "Build xong. Dùng tools/package.py để tạo factory và .vup từ hai thư mục build."
