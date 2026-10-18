// notify.cpp — обёртка над libnotify. При недоступности D-Bus (например, вне
// сессии sway) тихо деградирует: уведомления не отправляются, приложение работает.
#include "notify.h"

#include <libnotify/notify.h>

Notifier::Notifier() {
    notify_init("bluecal");
}

Notifier::~Notifier() {
    if (notify_is_initted()) notify_uninit();
}

bool Notifier::available() const {
    return notify_is_initted() && notify_get_server_caps() != nullptr;
}

void Notifier::send(const std::string& summary, const std::string& body) {
    if (!notify_is_initted()) return;
    NotifyNotification* n = notify_notification_new(summary.c_str(), body.c_str(), "dialog-information");
    if (!n) return;
    notify_notification_set_timeout(n, 10000); // 10 секунд
    GError* error = nullptr;
    notify_notification_show(n, &error);
    if (error) g_error_free(error);
    g_object_unref(n);
}
