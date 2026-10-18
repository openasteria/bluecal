// models.h — структуры данных календаря: заметки и напоминания.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct Note {
    int64_t id = 0;              // уникальный идентификатор
    std::string date;            // "ГГГГ-ММ-ДД"
    std::string text;            // текст заметки (многострочный)
    int64_t created = 0;         // unix-time создания
};

struct Reminder {
    int64_t id = 0;              // уникальный идентификатор
    std::string datetime;        // "ГГГГ-ММ-ДД HH:MM" — локальное время
    std::string title;           // название напоминания
    bool notified = false;       // отправлено ли уже уведомление
    int64_t created = 0;
};
