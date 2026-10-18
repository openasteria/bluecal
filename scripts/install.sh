#!/usr/bin/env bash
# install.sh — установка bluecal в ~/.local (без root, работает на Debian и других Linux).
set -euo pipefail
cd "$(dirname "$0")/.."

# --- Автоустановка зависимостей (Debian/Ubuntu и Arch) ---
need_pkgs=()
command -v cmake   >/dev/null 2>&1 || need_pkgs+=(cmake)
command -v g++     >/dev/null 2>&1 || command -v c++ >/dev/null 2>&1 || need_pkgs+=(g++)
command -v make    >/dev/null 2>&1 || need_pkgs+=(make)
# ncursesw и libnotify — проверяем по заголовкам/pc-файлам
if ! ls /usr/include/*/curses.h /usr/include/curses.h >/dev/null 2>&1; then
    need_pkgs+=(libncursesw5-dev)
fi
if ! pkg-config --exists libnotify >/dev/null 2>&1; then
    need_pkgs+=(libnotify-dev)
fi
if [ ${#need_pkgs[@]} -gt 0 ]; then
    echo "[0/4] Установка зависимостей: ${need_pkgs[*]}"
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        # в новых Debian пакет libncursesw5-dev переименован в libncurses-dev
        pkgs=("${need_pkgs[@]}")
        if apt-get --no-install-recommends install -y --dry-run libncurses-dev >/dev/null 2>&1 \
           && ! apt-cache show libncursesw5-dev >/dev/null 2>&1; then
            pkgs=("${pkgs[@]/libncursesw5-dev/libncurses-dev}")
        fi
        sudo apt-get install -y "${pkgs[@]}"
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -Sy --noconfirm base-devel ncurses libnotify
    else
        echo "Не удалось определить менеджер пакетов. Установите вручную: ${need_pkgs[*]}"
        exit 1
    fi
fi

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
