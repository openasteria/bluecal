// main.cpp — bluecal: тёмно-синий/голубой календарь с заметками и напоминаниями.
//
// Интерфейс (ncurses, 256 цветов):
//   • Календарная сетка с выделением текущего дня и выбранной даты.
//   • Панель заметок для выбранной даты.
//   • Фоновый поток проверяет напоминания и шлёт desktop-уведомления
//     через libnotify (работает в swaywm / любом Wayland/X11-сессии).
//
// Управление:
//   ← → ↑ ↓ / hjkl — навигация по дням; PageUp/PageDown или -/+ — месяц
//   n — новая заметка на выбранный день; N/d — удалить заметку
//   r — новое напоминание; D — удалить напоминание
//   q — выход
#include <ncurses.h>
#include <locale.h>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "color256.h"
#include "storage.h"
#include "notify.h"
#include "ui.h"

namespace {

// ---- палитра: brand #003689 + оттенки тёмно-синего/голубого ----
enum Ci {
    C_BG = 1,        // фон окна (#003689)
    C_FG,            // светлый голубой текст
    C_HEADER,        // заголовок месяца (яркий голубой)
    C_TODAY,         // текущий день (жёлтый поверх фона)
    C_SEL,           // выбранная дата (белый по светло-голубому)
    C_NOTE,          // дни с заметками (cyan)
    C_REM,           // дни с напоминаниями (magenta)
    C_DIM,           // приглушённый (названия дней недели)
    C_PANEL,         // панели
    C_STATUS,        // строка статуса
    C_EDIT,          // поле ввода
};

struct Palette {
    int bg = 0, fg = 7;
};

void init_colors() {
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    auto rgb = [](const char* hex) { return hex_to_rgb(hex); };

    int c_bg    = rgb_to_xterm256(rgb("#003689")); // фирменный #003689
    int c_fg    = rgb_to_xterm256(rgb("#BFE3FF")); // светло-голубой текст
    int c_hdr   = rgb_to_xterm256(rgb("#4FC3F7")); // яркий голубой
    int c_today = rgb_to_xterm256(rgb("#FFD54F")); // жёлтый
    int c_selbg = rgb_to_xterm256(rgb("#29B6F6")); // selected background
    int c_selfg = rgb_to_xterm256(rgb("#00224D")); // тёмно-синий на выборе
    int c_note  = rgb_to_xterm256(rgb("#00E5FF")); // cyan
    int c_rem   = rgb_to_xterm256(rgb("#FF80AB")); // magenta
    int c_dim   = rgb_to_xterm256(rgb("#7FB3D9")); // приглушённый голубой
    int c_panel = rgb_to_xterm256(rgb("#004A9E")); // чуть светлее фона
    int c_stat  = rgb_to_xterm256(rgb("#0A2E5C")); // тёмно-синий статус

    init_pair(C_BG,    c_fg,    c_bg);
    init_pair(C_HEADER,c_hdr,   -1);
    init_pair(C_TODAY, c_today, c_bg);
    init_pair(C_SEL,   c_selfg, c_selbg);
    init_pair(C_NOTE,  c_note,  c_bg);
    init_pair(C_REM,   c_rem,   c_bg);
    init_pair(C_DIM,   c_dim,   -1);
    init_pair(C_PANEL, c_fg,    c_panel);
    init_pair(C_STATUS,c_hdr,   c_stat);
    init_pair(C_EDIT,  c_today, c_stat);
}

std::string current_date_str() {
    time_t t = time(nullptr);
    struct tm lt{};
    localtime_r(&t, &lt);
    char buf[16];
    strftime(buf, sizeof buf, "%Y-%m-%d", &lt);
    return buf;
}

bool parse_date(const std::string& s, int& y, int& m, int& d) {
    return sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3;
}

int days_in_month(int y, int m) {
    static const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 30;
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}

// Первый день месяца: 0=Пн ... 6=Вс (недельный стандарт РФ/Европы)
int first_weekday(int y, int m) {
    struct tm t{};
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = 1;
    time_t tt = mktime(&t);
    struct tm lt{};
    localtime_r(&tt, &lt);
    return (lt.tm_wday + 6) % 7;
}

const char* MONTHS_RU[] = {
    "Январь","Февраль","Март","Апрель","Май","Июнь",
    "Июль","Август","Сентябрь","Октябрь","Ноябрь","Декабрь"
};
const char* WD_RU[] = {"Пн","Вт","Ср","Чт","Пт","Сб","Вс"};

// ---------- фоновый планировщик напоминаний ----------
class Scheduler {
public:
    Scheduler(Storage& st, Notifier& nt) : st_(st), nt_(nt) {}

    void start() {
        stop_ = false;
        th_ = std::thread([this] { loop(); });
    }
    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        if (th_.joinable()) th_.join();
    }
    // сколько уведомлений отправлено (для статус-бара)
    int sent() const { return sent_.load(); }

private:
    void loop() {
        while (true) {
            check();
            std::unique_lock<std::mutex> lk(mtx_);
            if (cv_.wait_for(lk, std::chrono::seconds(15), [this] { return stop_; }))
                break;
        }
    }

    void check() {
        time_t now = time(nullptr);
        struct tm lt{};
        localtime_r(&now, &lt);
        char stamp[20];
        strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &lt);
        std::string cur = stamp;

        std::vector<Reminder> due;
        {
            std::lock_guard<std::mutex> lk(stmtx_);
            for (auto& rm : st_.reminders) {
                if (!rm.notified && rm.datetime <= cur) due.push_back(rm);
            }
        }
        for (auto& rm : due) {
            nt_.send("⏰ Напоминание: " + rm.title, rm.datetime);
            st_.mark_notified(rm.id);
            sent_++;
        }
    }

public:
    std::mutex& storage_mutex() { return stmtx_; }

private:
    Storage& st_;
    Notifier& nt_;
    std::thread th_;
    std::mutex mtx_;
    std::mutex stmtx_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::atomic<int> sent_{0};
};

// ---------- ввод текста ----------
std::string prompt_line(WINDOW* win, const std::string& label) {
    int h, w;
    getmaxyx(win, h, w);
    std::string text;
    int row = h - 2;
    if (row < 1) row = 1;
    wmove(win, row, 1);
    wattroff(win, A_REVERSE);
    wclrtoeol(win);
    mvwprintw(win, row, 1, "%s", label.c_str());
    wrefresh(win);
    curs_set(1);
    while (true) {
        int ch = wgetch(win);
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) { text.clear(); break; } // Esc — отмена
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!text.empty()) text.pop_back();
        } else if (ch >= 32 && ch < 127) {
            text += static_cast<char>(ch);
        } else if (ch >= 0x80) {
            // UTF-8 многобайтовый символ: читаем стартовый байт + continuation
            int bytes = 0;
            if ((ch & 0xE0) == 0xC0) bytes = 1;
            else if ((ch & 0xF0) == 0xE0) bytes = 2;
            else if ((ch & 0xF8) == 0xF0) bytes = 3;
            std::string u;
            unsigned char lead = static_cast<unsigned char>(ch & 0xFF);
            u += static_cast<char>(lead);
            bool ok = true;
            for (int i = 0; i < bytes; ++i) {
                int c2 = wgetch(win);
                if (c2 == ERR) { ok = false; break; }
                u += static_cast<char>(c2 & 0xFF);
            }
            if (ok) text += u;
        }
        wmove(win, row, 1);
        wattroff(win, A_REVERSE);
        wclrtoeol(win);
        std::string shown = label + text;
        if ((int)shown.size() > w - 2) shown = shown.substr(shown.size() - (w - 2));
        mvwprintw(win, row, 1, "%s", shown.c_str());
        wrefresh(win);
    }
    curs_set(0);
    return text;
}

// ---------- главный класс приложения ----------
class App {
public:
    int run_impl() {
        set_escdelay(25);
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        init_colors();
        nodelay(stdscr, TRUE);

        if (!storage_.open()) { endwin(); return 1; }
        Notifier notifier;
        Scheduler sched(storage_, notifier);
        sched.start();

        WINDOW* cal_win = newwin(0, 0, 0, 0);
        keypad(cal_win, TRUE);

        time_t now = time(nullptr);
        struct tm lt{};
        localtime_r(&now, &lt);
        view_y_ = lt.tm_year + 1900; view_m_ = lt.tm_mon + 1;
        sel_y_ = view_y_; sel_m_ = view_m_; sel_d_ = lt.tm_mday;

        int status = 0;
        bool running = true;
        while (running) {
            draw(cal_win);
            wtimeout(cal_win, 1000); // тик раз в секунду — обновляем «сегодня»
            int ch = wgetch(cal_win);
            switch (ch) {
                case 'q': case 'Q': running = false; break;
                case KEY_LEFT:  case 'h': move_day(-1); break;
                case KEY_RIGHT: case 'l': move_day(+1); break;
                case KEY_UP:    case 'k': move_day(-7); break;
                case KEY_DOWN:  case 'j': move_day(+7); break;
                case KEY_PPAGE: case '-': change_month(-1); break;
                case KEY_NPAGE: case '+': case '=': change_month(+1); break;
                case 't': goto_today(); break;
                case 'n': add_note(); break;
                case 'N': delete_selected_note(); break;
                case 'r': add_reminder(); break;
                case 'D': delete_selected_reminder(); break;
                default: break;
            }
        }

        sched.shutdown();
        delwin(cal_win);
        endwin();
        return status;
    }

private:
    Storage storage_;
    int view_y_ = 2026, view_m_ = 1;
    int sel_y_ = 2026, sel_m_ = 1, sel_d_ = 1;
    int note_idx_ = 0;    // выбранная заметка в списке
    int rem_idx_ = 0;     // выбранное напоминание в списке

    std::string sel_date() const {
        char b[16]; snprintf(b, sizeof b, "%04d-%02d-%02d", sel_y_, sel_m_, sel_d_);
        return b;
    }

    void clamp_view() {
        if (view_m_ < 1)  { view_m_ = 12; --view_y_; }
        if (view_m_ > 12) { view_m_ = 1;  ++view_y_; }
    }

    void change_month(int delta) {
        view_m_ += delta;
        clamp_view();
        sel_y_ = view_y_; sel_m_ = view_m_;
        int dim = days_in_month(view_y_, view_m_);
        if (sel_d_ > dim) sel_d_ = dim;
    }

    void move_day(int delta) {
        // простой арифметический сдвиг через mktime
        struct tm t{};
        t.tm_year = sel_y_ - 1900; t.tm_mon = sel_m_ - 1; t.tm_mday = sel_d_;
        t.tm_isdst = -1;
        time_t tt = mktime(&t);
        tt += delta * 86400;
        struct tm lt{};
        localtime_r(&tt, &lt);
        sel_y_ = lt.tm_year + 1900; sel_m_ = lt.tm_mon + 1; sel_d_ = lt.tm_mday;
        view_y_ = sel_y_; view_m_ = sel_m_;
        note_idx_ = 0;
    }

    void goto_today() {
        time_t now = time(nullptr);
        struct tm lt{};
        localtime_r(&now, &lt);
        sel_y_ = lt.tm_year + 1900; sel_m_ = lt.tm_mon + 1; sel_d_ = lt.tm_mday;
        view_y_ = sel_y_; view_m_ = sel_m_;
        note_idx_ = 0;
    }

    std::vector<const Note*> notes_for(const std::string& date) {
        std::vector<const Note*> out;
        for (const auto& nt : storage_.notes)
            if (nt.date == date) out.push_back(&nt);
        return out;
    }

    std::vector<const Reminder*> reminders_for(const std::string& date) {
        std::vector<const Reminder*> out;
        for (const auto& rm : storage_.reminders)
            if (rm.datetime.size() >= 10 && rm.datetime.substr(0, 10) == date)
                out.push_back(&rm);
        return out;
    }

    bool day_has_note(const std::string& date) {
        for (const auto& nt : storage_.notes) if (nt.date == date) return true;
        return false;
    }
    bool day_has_reminder(const std::string& date) {
        for (const auto& rm : storage_.reminders)
            if (rm.datetime.size() >= 10 && rm.datetime.substr(0, 10) == date) return true;
        return false;
    }

    void add_note() {
        std::string text = prompt_line(stdscr, "Заметка: ");
        if (!text.empty()) {
            storage_.add_note(sel_date(), text);
            note_idx_ = static_cast<int>(notes_for(sel_date()).size()) - 1;
        }
        redrawwin(stdscr); touchwin(stdscr);
    }

    void delete_selected_note() {
        auto list = notes_for(sel_date());
        if (list.empty()) return;
        if (note_idx_ >= (int)list.size()) note_idx_ = (int)list.size() - 1;
        storage_.remove_note(list[note_idx_]->id);
        note_idx_ = 0;
        redrawwin(stdscr); touchwin(stdscr);
    }

    void add_reminder() {
        // формат: 2026-10-18 14:30 Название
        std::string line = prompt_line(stdscr, "Напоминание (ГГГГ-ММ-ДД ЧЧ:ММ Текст): ");
        if (line.empty()) return;
        int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
        char title[512] = {0};
        int parsed = sscanf(line.c_str(), "%d-%d-%d %d:%d %511[^\n]", &y, &mo, &d, &hh, &mi, title);
        std::string dt, ttl;
        if (parsed >= 5) {
            char b[24];
            snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", y, mo, d, hh, mi);
            dt = b;
            ttl = title;
        } else if (parsed >= 3) {
            // только дата — ставим 09:00
            char b[24];
            snprintf(b, sizeof b, "%04d-%02d-%02d 09:00", y, mo, d);
            dt = b;
            ttl = (parsed == 4 ? std::string(title) : std::string("Напоминание"));
        } else {
            beep();
            return;
        }
        if (ttl.empty()) ttl = "Напоминание";
        storage_.add_reminder(dt, ttl);
        redrawwin(stdscr); touchwin(stdscr);
    }

    void delete_selected_reminder() {
        auto list = reminders_for(sel_date());
        if (list.empty()) return;
        if (rem_idx_ >= (int)list.size()) rem_idx_ = (int)list.size() - 1;
        storage_.remove_reminder(list[rem_idx_]->id);
        rem_idx_ = 0;
        redrawwin(stdscr); touchwin(stdscr);
    }

    // ---------- отрисовка ----------
    void draw(WINDOW* cal_win) {
        erase();
        int LINES_, COLS_;
        getmaxyx(stdscr, LINES_, COLS_);

        // ===== левая колонка: календарь =====
        int cal_h = std::min(LINES_ - 2, 12);
        int cal_w = std::min(COLS_ / 2, 24);
        if (cal_w < 20) cal_w = std::min(COLS_, 20);

        attron(COLOR_PAIR(C_HEADER) | A_BOLD);
        mvprintw(0, 2, "%s %d", MONTHS_RU[view_m_ - 1], view_y_);
        attroff(COLOR_PAIR(C_HEADER) | A_BOLD);

        std::string today = current_date_str();

        attron(COLOR_PAIR(C_DIM));
        mvprintw(1, 1, " Пн Вт Ср Чт Пт Сб Вс");
        attroff(COLOR_PAIR(C_DIM));

        int fw = first_weekday(view_y_, view_m_);
        int dim = days_in_month(view_y_, view_m_);
        for (int day = 1; day <= dim; ++day) {
            int cell = fw + day - 1;
            int row = 2 + cell / 7;
            int col = 1 + (cell % 7) * 3;
            if (row > cal_h - 1) continue;

            char b[16]; snprintf(b, sizeof b, "%04d-%02d-%02d", view_y_, view_m_, day);
            std::string ds = b;
            bool is_today = (ds == today);
            bool is_sel = (view_y_ == sel_y_ && view_m_ == sel_m_ && day == sel_d_);
            bool has_n = day_has_note(ds);
            bool has_r = day_has_reminder(ds);

            char buf[8];
            snprintf(buf, sizeof buf, "%2d ", day);

            attron(COLOR_PAIR(C_BG));
            if (is_sel) {
                attron(COLOR_PAIR(C_SEL) | A_BOLD);
                mvprintw(row, col, "%s", buf);
                attroff(A_BOLD);
                attron(COLOR_PAIR(C_BG));
            } else if (is_today) {
                attron(COLOR_PAIR(C_TODAY) | A_BOLD | A_REVERSE);
                mvprintw(row, col, "%s", buf);
                attroff(A_BOLD | A_REVERSE);
                attron(COLOR_PAIR(C_BG));
            } else {
                int pair = C_BG;
                if (has_n && has_r) pair = C_REM;
                else if (has_r) pair = C_REM;
                else if (has_n) pair = C_NOTE;
                attron(COLOR_PAIR(pair));
                if (has_n || has_r) attron(A_BOLD);
                mvprintw(row, col, "%s", buf);
                if (has_n || has_r) attroff(A_BOLD);
            }
            attroff(COLOR_PAIR(C_BG));
        }

        // легенда под календарём
        int leg_row = std::min(cal_h, LINES_ - 3) + 1;
        attron(COLOR_PAIR(C_BG));
        mvprintw(leg_row, 1, " ■ сегодня ■ заметка ■ напом.");
        attroff(COLOR_PAIR(C_BG));

        // ===== правая колонка: заметки и напоминания =====
        int rx = cal_w + 3;
        if (rx < COLS_) {
            int rw = COLS_ - rx - 1;
            std::string head = " " + sel_date() + " ";
            attron(COLOR_PAIR(C_PANEL) | A_BOLD);
            mvprintw(0, rx, "%-*.*s", rw, rw, head.c_str());
            attroff(COLOR_PAIR(C_PANEL) | A_BOLD);

            auto notes = notes_for(sel_date());
            auto rems  = reminders_for(sel_date());

            int ry = 2;
            attron(COLOR_PAIR(C_HEADER) | A_BOLD);
            mvprintw(ry, rx, "%-*.*s", rw, rw, " 📝 Заметки");
            attroff(COLOR_PAIR(C_HEADER) | A_BOLD);
            ++ry;
            if (notes.empty()) {
                attron(COLOR_PAIR(C_DIM));
                mvprintw(ry, rx + 1, "(нет — нажмите n)");
                attroff(COLOR_PAIR(C_DIM));
                ++ry;
            } else {
                for (size_t i = 0; i < notes.size() && ry < LINES_ - 6; ++i) {
                    std::string one = notes[i]->text;
                    size_t nl = one.find('\n');
                    if (nl != std::string::npos) one = one.substr(0, nl) + "…";
                    if (static_cast<int>(i) == note_idx_) attron(COLOR_PAIR(C_SEL) | A_BOLD);
                    else attron(COLOR_PAIR(C_NOTE));
                    mvprintw(ry, rx + 1, "%-*.*s", rw - 2, rw - 2, ("› " + one).c_str());
                    if (static_cast<int>(i) == note_idx_) attroff(A_BOLD);
                    attroff(COLOR_PAIR(C_SEL)); attroff(COLOR_PAIR(C_NOTE));
                    ++ry;
                }
            }

            ry += 1;
            attron(COLOR_PAIR(C_HEADER) | A_BOLD);
            mvprintw(ry, rx, "%-*.*s", rw, rw, " ⏰ Напоминания");
            attroff(COLOR_PAIR(C_HEADER) | A_BOLD);
            ++ry;
            if (rems.empty()) {
                attron(COLOR_PAIR(C_DIM));
                mvprintw(ry, rx + 1, "(нет — нажмите r)");
                attroff(COLOR_PAIR(C_DIM));
                ++ry;
            } else {
                for (size_t i = 0; i < rems.size() && ry < LINES_ - 2; ++i) {
                    std::string hm = rems[i]->datetime.size() >= 16
                                   ? rems[i]->datetime.substr(11, 5) : "--:--";
                    std::string mark = rems[i]->notified ? "✓ " : "• ";
                    std::string line = mark + hm + "  " + rems[i]->title;
                    if (static_cast<int>(i) == rem_idx_) attron(COLOR_PAIR(C_SEL) | A_BOLD);
                    else attron(COLOR_PAIR(C_REM));
                    mvprintw(ry, rx + 1, "%-*.*s", rw - 2, rw - 2, line.c_str());
                    if (static_cast<int>(i) == rem_idx_) attroff(A_BOLD);
                    attroff(COLOR_PAIR(C_SEL)); attroff(COLOR_PAIR(C_REM));
                    ++ry;
                }
            }
        }

        // ===== нижняя строка статуса =====
        attron(COLOR_PAIR(C_STATUS));
        std::string help = " ←→↑↓ день  -/+ месяц  n заметка  N удал.  r напоминание  D удал.  t сегодня  q выход ";
        mvprintw(LINES_ - 1, 0, "%-*.*s", COLS_, COLS_, help.c_str());
        attroff(COLOR_PAIR(C_STATUS));

        refresh();
    }
};

} // namespace

int CalendarApp::run() {
    App app;
    return app.run_impl();
}

int main() {
    setlocale(LC_ALL, "");
    CalendarApp app;
    return app.run();
}
