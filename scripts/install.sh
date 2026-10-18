#!/usr/bin/env bash
# install.sh — установка bluecal в ~/.local (без root, работает на Debian и других Linux).
set -euo pipefail
cd "$(dirname "$0")/.."

echo "[1/4] Сборка (cmake + make)…"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo "[2/4] Установка в ~/.local…"
mkdir -p "$HOME/.local/bin"
cp build/bluecal "$HOME/.local/bin/bluecal"
cp scripts/bluecal-reminderd "$HOME/.local/bin/bluecal-reminderd"
chmod +x "$HOME/.local/bin/bluecal-reminderd"

echo "[3/4] User-сервис systemd для напоминаний (работает под swaywm)…"
mkdir -p "$HOME/.config/systemd/user"
sed "s#%h/.local/bin#$HOME/.local/bin#" scripts/bluecal.service \
    > "$HOME/.config/systemd/user/bluecal.service"
systemctl --user daemon-reload || true
systemctl --user enable --now bluecal.service || true

echo "[4/4] Готово."
echo "  Запуск календаря:   bluecal"
echo "  Статус демона:      systemctl --user status bluecal"
case ":$PATH:" in
    *":$HOME/.local/bin:"*) ;;
    *) echo "⚠ Добавьте в PATH: export PATH=\"\$HOME/.local/bin:\$PATH\"";;
esac
