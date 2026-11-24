#!/bin/bash
set -euo pipefail
task_dir="$(cd "$(dirname "$0")" && pwd)"
shopt -s nullglob
for task_python in "${VINYL_PYTHON:-}" "$task_dir/.flash-venv/bin/python3" "$HOME"/.espressif/python_env/*/bin/python "$HOME/.platformio/penv/bin/python" python3; do
    if [[ -n "$task_python" ]] && "$task_python" -c 'import esptool, serial; assert int(esptool.__version__.split(".")[0]) in (4, 5)' >/dev/null 2>&1; then
        exec "$task_python" "$task_dir/flash.py" "$@"
    fi
done
if ! command -v python3 >/dev/null 2>&1; then
    echo "Cần Python 3 trên Mac để nạp. Cài Python 3 rồi chạy lại lệnh này."
    exit 1
fi
echo "Chưa có esptool: tạo môi trường riêng trong .flash-venv (cần Internet lần đầu)."
python3 -m venv "$task_dir/.flash-venv"
"$task_dir/.flash-venv/bin/python3" -m pip install --disable-pip-version-check 'esptool==4.10.0' 'pyserial==3.5'
exec "$task_dir/.flash-venv/bin/python3" "$task_dir/flash.py" "$@"
