// notify.h — уведомления рабочего стола через libnotify (desktop-notifications).
// Работает в swaywm/sway, так как sway предоставляет сервер уведомлений.
#pragma once
#include <string>

class Notifier {
public:
    // Инициализирует libnotify (без GUI-зависимостей).
    Notifier();
    ~Notifier();

    // Возвращает true, если удалённый интерфейс libnotify доступен.
    bool available() const;

    // Отправляет уведомление (summary — заголовок, body — текст).
    // title — имя приложения ("bluecal").
    void send(const std::string& summary, const std::string& body);

private:
    struct Notify* handle_ = nullptr;
};
