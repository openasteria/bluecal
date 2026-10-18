# bluecal

Тёмно-синий (акцент #003689) терминальный календарь с заметками и напоминаниями
в desktop-уведомлениях. Работает на Debian/Ubuntu/Fedora/Arch и в swaywm
(уведомления через libnotify → mako/dunst по D-Bus).

## Возможности
- Месячный календарь, навигация стрелками/HJKL, `t` — сегодня
- Заметки на дни (`n`) со списком и удалением (`d`)
- Напоминания с датой/временем (`r`) — фоновый демон шлёт уведомления
- Хранение: `~/.config/bluecal/data.json`

## Сборка
```bash
sudo apt install build-essential cmake libncursesw5-dev libnotify-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
sudo cmake --install build
```

## Запуск
```bash
bluecal                 # интерфейс
bluecal-reminderd       # демон напоминаний (или systemd user-сервис)
systemctl --user enable --now bluecal.service
```

В sway добавьте в ~/.sway/config при необходимости:
`exec bluecal-reminderd`
