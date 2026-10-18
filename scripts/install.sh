#!/usr/bin/env bash
# install.sh — установка bluecal в ~/.local (без root, работает на Debian и других Linux).
set -euo pipefail
cd "$(dirname "$0")/.."

# --- Автоустановка зависимостей (Debian/Ubuntu, Arch, Fedora/openSUSE) ---

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then SUDO="sudo"
    else echo "Нет root и нет sudo. Установите пакеты вручную."; exit 1; fi
fi

apt_install_pkg() {  # $1 = имя пакета; ставит только если реально доступен в репозитории
    apt-cache show "$1" >/dev/null 2>&1 || return 1
    $SUDO apt-get install -y "$1"
}

apt_updated=0
apt_update_once() {
    if [ "$apt_updated" = "0" ]; then
        # ВАЖНО: без «apt-get update» на свежей системе списки пакетов пусты —
        # отсюда ошибка «E: Unable to locate package …»
        $SUDO apt-get update
        apt_updated=1
    fi
}

have_toolchain() {
    command -v cc   >/dev/null 2>&1 && \
    command -v make >/dev/null 2>&1 && \
    command -v cmake >/dev/null 2>&1
}

have_libs() {
    ls /usr/include/curses.h /usr/include/*/curses.h >/dev/null 2>&1 && \
    pkg-config --exists libnotify >/dev/null 2>&1
}

if ! have_toolchain || ! have_libs; then
    echo "[0/4] Установка зависимостей…"
    if command -v apt-get >/dev/null 2>&1; then
        apt_update_once
        have_toolchain || {
            apt_install_pkg build-essential || apt_install_pkg clang || true
            apt_install_pkg make  || true
            apt_install_pkg cmake || true
        }
        if ! have_libs; then
            # в Debian 12+ libncursesw5-dev переименован в libncurses-dev — пробуем оба
            apt_install_pkg libncurses-dev || apt_install_pkg libncursesw5-dev || true
            apt_install_pkg libnotify-dev || true
        fi
    elif command -v pacman >/dev/null 2>&1; then
        $SUDO pacman -Sy --noconfirm base-devel cmake ncurses libnotify
    elif command -v dnf >/dev/null 2>&1; then
        $SUDO dnf install -y gcc-c++ make cmake ncurses-devel libnotify-devel
    elif command -v zypper >/dev/null 2>&1; then
        $SUDO zypper --non-interactive install patterns-devel-base-devel_basis cmake \
            libncursesw-devel libnotify-devel
    else
        echo "Не удалось определить менеджер пакетов."
        exit 1
    fi
fi

# Финальная проверка: если чего-то всё ещё нет — понятная ошибка вместо cryptic fail
missing=""
command -v cmake  >/dev/null 2>&1 || missing="$missing cmake"
ls /usr/include/curses.h /usr/include/*/curses.h >/dev/null 2>&1 || missing="$missing libncurses-dev"
pkg-config --exists libnotify >/dev/null 2>&1 || missing="$missing libnotify-dev"
if [ -n "$missing" ]; then
    echo "ОШИБКА: не удалось установить:$missing"
    echo "Проверьте интернет и что в источниках (/etc/apt/sources.list или sources.list.d)"
    echo "подключен компонент main, затем повторите: ./scripts/install.sh"
    exit 1
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
