// storage.h — JSON-хранилище заметок и напоминаний (~/.config/bluecal).
#pragma once
#include <string>
#include <vector>
#include "models.h"

class Storage {
public:
    // Открывает (или создаёт) файл ~/.config/bluecal/data.json.
    // Возвращает false при ошибке записи/чтения (кроме случая «файла нет» — это ок).
    bool open(std::string* err = nullptr);

    const std::string& path() const { return file_; }

    std::vector<Note>     notes;
    std::vector<Reminder> reminders;

    // Добавляет заметку (присваивает id) и сохраняет на диск.
    void add_note(const std::string& date, const std::string& text);
    // Удаляет заметку по id и сохраняет.
    void remove_note(int64_t id);

    // Добавляет напоминание (присваивает id) и сохраняет на диск.
    void add_reminder(const std::string& datetime, const std::string& title);
    void remove_reminder(int64_t id);
    void mark_notified(int64_t id);

    // Сохраняет всё в файл (атомарно: temp + rename).
    bool save(std::string* err = nullptr);

private:
    std::string file_;
    int64_t next_id_ = 1;

    void load();
};
