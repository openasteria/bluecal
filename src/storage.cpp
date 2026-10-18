// storage.cpp — простейший JSON-сериализатор/парсер для данных календаря.
#include "storage.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <sstream>

// ---------- helpers ----------
static std::string json_escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else out += static_cast<char>(c);
        }
    }
    return out;
}

static std::string json_unescape(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            char n = raw[++i];
            switch (n) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '"': out += '"';  break;
                case '\\':out += '\\'; break;
                case '/': out += '/';  break;
                case 'u': {
                    // поддерживаем только базовые BMP-последовательности \uXXXX
                    std::string hexv = raw.substr(i + 1, 4);
                    i += 4;
                    int cp = static_cast<int>(strtol(hexv.c_str(), nullptr, 16));
                    // кодируем в UTF-8
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += n;
            }
        } else out += c;
    }
    return out;
}

// Минималистичный поиск строкового поля вида  "key":"value"  внутри блока.
static bool find_string_field(const std::string& text, size_t from, size_t to,
                              const std::string& key, std::string& value) {
    std::string pat = "\"" + key + "\":\"";
    size_t p = text.find(pat, from);
    if (p == std::string::npos || p >= to) return false;
    p += pat.size();
    std::string raw;
    while (p < to) {
        char c = text[p];
        if (c == '\\' && p + 1 < to) { raw += c; raw += text[p + 1]; p += 2; continue; }
        if (c == '"') break;
        raw += c;
        ++p;
    }
    value = json_unescape(raw);
    return true;
}

static bool find_int_field(const std::string& text, size_t from, size_t to,
                           const std::string& key, long long& value) {
    std::string pat = "\"" + key + "\":";
    size_t p = text.find(pat, from);
    if (p == std::string::npos || p >= to) return false;
    p += pat.size();
    std::string num;
    while (p < to && (isdigit(text[p]) || text[p] == '-')) num += text[p++];
    if (num.empty()) return false;
    value = strtoll(num.c_str(), nullptr, 10);
    return true;
}

static bool find_bool_field(const std::string& text, size_t from, size_t to,
                            const std::string& key, bool& value) {
    std::string pat = "\"" + key + "\":";
    size_t p = text.find(pat, from);
    if (p == std::string::npos || p >= to) return false;
    p += pat.size();
    value = (text.compare(p, 4, "true") == 0);
    return true;
}

// Разбивает массив JSON "[ {...}, {...} ]" на блоки объектов верхнего уровня.
static std::vector<std::pair<size_t, size_t>> split_objects(const std::string& text,
                                                            const std::string& array_key) {
    std::vector<std::pair<size_t, size_t>> blocks;
    std::string pat = "\"" + array_key + "\":[";
    size_t start = text.find(pat);
    if (start == std::string::npos) return blocks;
    start += pat.size();
    size_t i = start;
    int depth = 0;
    size_t obj_begin = std::string::npos;
    bool in_str = false;
    for (; i < text.size(); ++i) {
        char c = text[i];
        if (in_str) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{') { if (depth == 0) obj_begin = i; ++depth; continue; }
        if (c == '}') {
            --depth;
            if (depth == 0 && obj_begin != std::string::npos) {
                blocks.emplace_back(obj_begin, i + 1);
                obj_begin = std::string::npos;
            }
            continue;
        }
        if (c == ']' && depth == 0) break;
    }
    return blocks;
}

// ---------- Storage ----------

bool Storage::open(std::string* err) {
    const char* home = getenv("HOME");
    std::string dir = (home ? home : "/tmp");
    dir += "/.config/bluecal";
    mkdir(dir.c_str(), 0755);
    file_ = dir + "/data.json";

    FILE* f = fopen(file_.c_str(), "r");
    if (!f) {
        // файла ещё нет — создадим пустой при первом сохранении
        notes.clear();
        reminders.clear();
        return true;
    }
    std::stringstream ss;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) ss.write(buf, n);
    fclose(f);
    std::string text = ss.str();

    load();
    // восстановление next_id
    long long maxid = 0;
    auto scan = [&](const std::string& key) {
        for (auto& [b, e] : split_objects(text, key)) {
            long long id = 0;
            if (find_int_field(text, b, e, "id", id) && id > maxid) maxid = id;
        }
    };
    scan("notes");
    scan("reminders");
    next_id_ = maxid + 1;
    (void)err;
    return true;
}

void Storage::load() {
    notes.clear();
    reminders.clear();

    FILE* f = fopen(file_.c_str(), "r");
    if (!f) return;
    std::stringstream ss;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) ss.write(buf, n);
    fclose(f);
    std::string text = ss.str();

    for (auto& [b, e] : split_objects(text, "notes")) {
        Note nt;
        long long id = 0, cr = 0;
        std::string date, txt;
        find_int_field(text, b, e, "id", id);
        find_int_field(text, b, e, "created", cr);
        find_string_field(text, b, e, "date", date);
        find_string_field(text, b, e, "text", txt);
        nt.id = id; nt.created = cr; nt.date = date; nt.text = txt;
        notes.push_back(nt);
    }
    for (auto& [b, e] : split_objects(text, "reminders")) {
        Reminder rm;
        long long id = 0, cr = 0;
        std::string dt, title;
        bool notified = false;
        find_int_field(text, b, e, "id", id);
        find_int_field(text, b, e, "created", cr);
        find_string_field(text, b, e, "datetime", dt);
        find_string_field(text, b, e, "title", title);
        find_bool_field(text, b, e, "notified", notified);
        rm.id = id; rm.created = cr; rm.datetime = dt; rm.title = title; rm.notified = notified;
        reminders.push_back(rm);
    }
}

bool Storage::save(std::string* err) {
    std::string tmp = file_ + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) { if (err) *err = "не удалось открыть файл для записи"; return false; }
    fprintf(f, "{\n  \"notes\": [\n");
    for (size_t i = 0; i < notes.size(); ++i) {
        const Note& nt = notes[i];
        fprintf(f, "    {\"id\": %lld, \"date\": \"%s\", \"text\": \"%s\", \"created\": %lld}%s\n",
                (long long)nt.id, json_escape(nt.date).c_str(),
                json_escape(nt.text).c_str(), (long long)nt.created,
                i + 1 < notes.size() ? "," : "");
    }
    fprintf(f, "  ],\n  \"reminders\": [\n");
    for (size_t i = 0; i < reminders.size(); ++i) {
        const Reminder& rm = reminders[i];
        fprintf(f,
                "    {\"id\": %lld, \"datetime\": \"%s\", \"title\": \"%s\", \"notified\": %s, \"created\": %lld}%s\n",
                (long long)rm.id, json_escape(rm.datetime).c_str(),
                json_escape(rm.title).c_str(), rm.notified ? "true" : "false",
                (long long)rm.created,
                i + 1 < reminders.size() ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    if (rename(tmp.c_str(), file_.c_str()) != 0) {
        if (err) *err = "ошибка сохранения";
        return false;
    }
    return true;
}

void Storage::add_note(const std::string& date, const std::string& text) {
    Note nt;
    nt.id = next_id_++;
    nt.date = date;
    nt.text = text;
    nt.created = time(nullptr);
    notes.push_back(nt);
    save();
}

void Storage::remove_note(int64_t id) {
    for (size_t i = 0; i < notes.size(); ++i)
        if (notes[i].id == id) { notes.erase(notes.begin() + i); break; }
    save();
}

void Storage::add_reminder(const std::string& datetime, const std::string& title) {
    Reminder rm;
    rm.id = next_id_++;
    rm.datetime = datetime;
    rm.title = title;
    rm.notified = false;
    rm.created = time(nullptr);
    reminders.push_back(rm);
    save();
}

void Storage::remove_reminder(int64_t id) {
    for (size_t i = 0; i < reminders.size(); ++i)
        if (reminders[i].id == id) { reminders.erase(reminders.begin() + i); break; }
    save();
}

void Storage::mark_notified(int64_t id) {
    for (auto& rm : reminders)
        if (rm.id == id) { rm.notified = true; break; }
    save();
}
