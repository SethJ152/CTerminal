// main.cpp
// Tiny Mint-inspired terminal (colors + extra commands).
// Cross-platform (POSIX + Windows best-effort).

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <system_error>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <thread>
#include <map>
#include <random>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <cstring>   // <<-- fixed: strlen, memset, etc.

#ifdef _WIN32
  #include <windows.h>
  #include <shellapi.h>
  #define popen _popen
  #define pclose _pclose
  #define PLATFORM "Windows"
#else
  #include <unistd.h>
  #include <sys/statvfs.h>
  #include <sys/types.h>
  #include <pwd.h>
  #include <sys/stat.h>
  #define PLATFORM "POSIX"
#endif

using namespace std;
namespace fs = std::filesystem;

// -- state ---------------------------------------------------------
static vector<string> history_buf;
static map<string,string> alias_map;
static map<string,string> bookmarks;

// -- Mint-inspired palette & helpers ------------------------------------
enum class MTColor {
    RESET,
    BOLD,
    DIM,
    MINT_GREEN,
    BRIGHT_GREEN,
    CYAN,
    BLUE,
    MAGENTA,
    ORANGE,
    YELLOW,
    RED,
    GRAY
};

static string mt_code(MTColor c) {
#ifdef _WIN32
    // We'll still return ANSI codes; enable VT on startup
#endif
    switch (c) {
        case MTColor::RESET:        return "\x1b[0m";
        case MTColor::BOLD:         return "\x1b[1m";
        case MTColor::DIM:          return "\x1b[2m";
        case MTColor::MINT_GREEN:   return "\x1b[38;5;121m";
        case MTColor::BRIGHT_GREEN: return "\x1b[92m";
        case MTColor::CYAN:         return "\x1b[36m";
        case MTColor::BLUE:         return "\x1b[34m";
        case MTColor::MAGENTA:      return "\x1b[35m";
        case MTColor::ORANGE:       return "\x1b[38;5;214m";
        case MTColor::YELLOW:       return "\x1b[33m";
        case MTColor::RED:          return "\x1b[31m";
        case MTColor::GRAY:         return "\x1b[90m";
        default:                    return "\x1b[0m";
    }
}

static void enable_ansi_on_windows() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif
}

static string colorize(MTColor c, const string &s) {
    return mt_code(c) + s + mt_code(MTColor::RESET);
}
static void print_colored(MTColor c, const string &s) { cout << colorize(c, s); }
static void eprint_colored(MTColor c, const string &s) { cerr << colorize(c, s); }

// -- small helpers ---------------------------------------------------------
static vector<string> split_args(const string &s) {
    vector<string> out;
    istringstream iss(s);
    string token;
    while (iss >> token) {
        if ((token.front() == '"' && token.back() != '"') ||
            (token.front() == '\'' && token.back() != '\'')) {
            char quote = token.front();
            string rest;
            while (iss >> rest) {
                token += " " + rest;
                if (!rest.empty() && rest.back() == quote) break;
            }
        }
        if (token.size() >= 2 &&
            ((token.front() == '"' && token.back() == '"') || (token.front() == '\'' && token.back() == '\'')))
            token = token.substr(1, token.size()-2);
        out.push_back(token);
    }
    return out;
}

static bool is_executable_file(const fs::path &p) {
#ifdef _WIN32
    string ext = p.has_extension() ? p.extension().string() : string();
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    static const vector<string> exts = { ".exe", ".com", ".bat", ".cmd" };
    return fs::is_regular_file(p) && find(exts.begin(), exts.end(), ext) != exts.end();
#else
    if (!fs::exists(p) || !fs::is_regular_file(p)) return false;
    return access(p.string().c_str(), X_OK) == 0;
#endif
}

static string perms_to_string(fs::perms p) {
    string s = "---------";
    s[0] = ( (p & fs::perms::owner_read) != fs::perms::none ? 'r' : '-');
    s[1] = ( (p & fs::perms::owner_write) != fs::perms::none ? 'w' : '-');
    s[2] = ( (p & fs::perms::owner_exec) != fs::perms::none ? 'x' : '-');
    s[3] = ( (p & fs::perms::group_read) != fs::perms::none ? 'r' : '-');
    s[4] = ( (p & fs::perms::group_write) != fs::perms::none ? 'w' : '-');
    s[5] = ( (p & fs::perms::group_exec) != fs::perms::none ? 'x' : '-');
    s[6] = ( (p & fs::perms::others_read) != fs::perms::none ? 'r' : '-');
    s[7] = ( (p & fs::perms::others_write) != fs::perms::none ? 'w' : '-');
    s[8] = ( (p & fs::perms::others_exec) != fs::perms::none ? 'x' : '-');
    return s;
}

static string file_time_string(const fs::file_time_type &ft) {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(ft - fs::file_time_type::clock::now()
                    + system_clock::now());
    time_t tt = system_clock::to_time_t(sctp);
    char buf[64]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&tt));
    return string(buf);
}

// -- core commands ---------------------------------------------------------

static void cmd_help() {
    print_colored(MTColor::CYAN, "Commands (Mint look):\n");
    cout <<
    "  help, exit, quit           - this message / quit\n"
    "  ls [dir]                   - list directory\n"
    "  ls -l [dir]                - long listing (permissions, size, mtime)\n"
    "  pwd                        - print working dir\n"
    "  cd <dir>                   - change dir\n"
    "  cat <file>                 - show file\n"
    "  edit <file>                - open file with $EDITOR/code/nano\n"
    "  echo <text>                - print text\n"
    "  history                    - show command history\n"
    "  history -c                 - clear history\n"
    "  grep <pat> <file>          - search for pattern in file\n"
    "  wc <file>                  - count lines/words/chars\n"
    "  head <file>                - first 10 lines\n"
    "  tail <file>                - last 10 lines\n"
    "  tail -f <file>             - follow appended writes (Ctrl-C to stop)\n"
    "  chmod <octal> <file>       - change permissions (e.g. 755)\n"
    "  ln <target> <link>         - create symbolic link\n"
    "  du [dir]                   - disk usage (simple)\n"
    "  sort <file>                - sort file lines\n"
    "  uniq <file>                - unique adjacent lines\n"
    "  tree [dir]                 - tree view (simple)\n"
    "  ps                         - process list\n"
    "  df                         - disk/free info\n"
    "  whoami                     - current user\n"
    "  date                       - show date/time\n"
    "  clear                      - clear screen\n"
    "  which <cmd>                - find executable in PATH\n"
    "  open <file>                - open with default application\n"
    "  env                        - show environment variables\n"
    "  setenv NAME VALUE          - set environment variable\n"
    "  stat <file>                - show file metadata\n"
    "  count [dir]                - count files and directories (recursive)\n"
    "  alias name='command'       - create alias\n"
    "  unalias name               - remove alias\n"
    "  aliases                    - list aliases\n"
    "  bookmark <name>            - save cwd under <name>\n"
    "  bookmarks                  - list bookmarks\n"
    "  goto <name>                - cd to bookmark\n"
    "  replace <file> <old> <new> - in-file simple replace (creates .bak)\n"
    "  uptime                     - show system uptime\n"
    "  ping <host> [-c N]         - wrapper around system ping\n"
    "  hash <file>                - show SHA-256 (system tool)\n"
    "  compress <file> <out.zip>  - wrapper to create archive\n"
    "  extract <archive>          - extract archive (unzip/tar)\n"
    "  top                        - launch top/htop/taskmgr\n"
    "  net                        - show network interfaces (ip/ipconfig)\n"
    "  notify <message>           - desktop notification (Linux)\n"
    "  calc \"expr\"               - simple calculator (+ - * / parentheses)\n"
    "  random [min] [max] [count] - generate integers\n";
}

static void cmd_ls(const vector<string>& a) {
    string p = ".";
    bool longlist = false;
    if (a.size() > 1) {
        if (a[1] == "-l") { longlist = true; if (a.size() > 2) p = a[2]; }
        else p = a[1];
    }
    try {
        vector<fs::directory_entry> ents;
        for (auto &e : fs::directory_iterator(p)) ents.push_back(e);
        sort(ents.begin(), ents.end(), [](auto &A, auto &B){
            return A.path().filename().string() < B.path().filename().string();
        });
        for (auto &e : ents) {
            string name = e.path().filename().string();
            if (longlist) {
                fs::file_status st = e.status();
                string perms = perms_to_string(st.permissions());
                uintmax_t sz = 0;
                try { if (fs::is_regular_file(e)) sz = fs::file_size(e); } catch(...) {}
                string mtime = file_time_string(fs::last_write_time(e));
                // print perms (gray), size (orange), mtime(gray)
                cout << colorize(MTColor::GRAY, perms + " ");
                {
                    ostringstream oss;
                    oss << setw(8) << sz;
                    cout << colorize(MTColor::ORANGE, oss.str()) << " ";
                }
                cout << colorize(MTColor::GRAY, mtime) << " ";
            }
            if (fs::is_directory(e)) cout << colorize(MTColor::BLUE, name) << '\n';
            else if (fs::is_symlink(e)) cout << colorize(MTColor::MAGENTA, name) << '\n';
            else if (is_executable_file(e.path())) cout << colorize(MTColor::BRIGHT_GREEN, name) << '\n';
            else cout << name << '\n';
        }
    } catch (const exception &ex) { eprint_colored(MTColor::RED, string("ls: ") + ex.what() + "\n"); }
}

static void cmd_pwd() {
    try { cout << colorize(MTColor::MINT_GREEN, fs::current_path().string()) << '\n'; } catch(...) { eprint_colored(MTColor::RED, "?\n"); }
}

static void cmd_cd(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "cd: missing arg\n"); return; }
    try { fs::current_path(a[1]); } catch (const exception &ex) { eprint_colored(MTColor::RED, string("cd: ") + ex.what() + '\n'); }
}

static void cmd_cat(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "cat: missing file\n"); return; }
    ifstream f(a[1]); if (!f) { eprint_colored(MTColor::RED, "cat: cannot open\n"); return; }
    string line; while (getline(f, line)) cout << line << '\n';
}

static void cmd_edit(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "edit: missing file\n"); return; }
    string file = a[1];
    const char* ed = getenv("EDITOR");
    string cmd;
    if (ed && strlen(ed)) cmd = string(ed) + " \"" + file + "\"";
    else {
        // try code, nano
        if (system("command -v code >/dev/null 2>&1") == 0) cmd = "code \"" + file + "\"";
        else cmd = "nano \"" + file + "\"";
    }
    system(cmd.c_str());
}

static void cmd_mkdir(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "mkdir: missing dir\n"); return; }
    try {
        if (a.size() > 1 && a[1] == "-p") {
            if (a.size() < 3) { eprint_colored(MTColor::YELLOW, "mkdir -p: missing path\n"); return; }
            fs::create_directories(a[2]);
            cout << "created\n";
        } else {
            if (fs::create_directory(a[1])) cout << "created\n"; else eprint_colored(MTColor::RED, "mkdir: failed\n");
        }
    } catch (const exception &ex) { eprint_colored(MTColor::RED, string("mkdir: ") + ex.what() + '\n'); }
}

static void cmd_rm(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "rm: missing file\n"); return; }
    try { if (fs::remove(a[1])) cout << "removed\n"; else eprint_colored(MTColor::RED, "rm: failed\n"); }
    catch (const exception &ex) { eprint_colored(MTColor::RED, string("rm: ") + ex.what() + '\n'); }
}

static void cmd_rmdir(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "rmdir: missing dir\n"); return; }
    try { uintmax_t n = fs::remove_all(a[1]); cout << "removed " << n << " entries\n"; }
    catch (const exception &ex) { eprint_colored(MTColor::RED, string("rmdir: ") + ex.what() + '\n'); }
}

static void cmd_touch(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "touch: missing file\n"); return; }
    ofstream f(a[1], ios::app); if (!f) eprint_colored(MTColor::RED, "touch: cannot create\n");
}

static void cmd_cp(const vector<string>& a) {
    if (a.size() < 3) { eprint_colored(MTColor::YELLOW, "cp: usage cp <src> <dst>\n"); return; }
    try { fs::copy(a[1], a[2], fs::copy_options::recursive | fs::copy_options::overwrite_existing); cout << "copied\n"; }
    catch (const exception &ex) { eprint_colored(MTColor::RED, string("cp: ") + ex.what() + '\n'); }
}

static void cmd_mv(const vector<string>& a) {
    if (a.size() < 3) { eprint_colored(MTColor::YELLOW, "mv: usage mv <src> <dst>\n"); return; }
    try { fs::rename(a[1], a[2]); cout << "moved\n"; }
    catch (const exception &ex) { eprint_colored(MTColor::RED, string("mv: ") + ex.what() + '\n'); }
}

static void cmd_find(const vector<string>& a) {
    string p = "."; if (a.size() > 1) p = a[1];
    try { for (auto &e : fs::recursive_directory_iterator(p)) cout << e.path().string() << '\n'; }
    catch (const exception &ex) { eprint_colored(MTColor::RED, string("find: ") + ex.what() + '\n'); }
}

static void print_tree(const fs::path &root, const string &prefix = "") {
    vector<fs::path> dirs, files;
    for (auto &e : fs::directory_iterator(root)) { if (fs::is_directory(e)) dirs.push_back(e.path()); else files.push_back(e.path()); }
    sort(dirs.begin(), dirs.end()); sort(files.begin(), files.end());
    for (size_t i = 0; i < dirs.size(); ++i) { bool last = (i+1==dirs.size()) && files.empty(); cout << prefix << (last?"└── ":"├── ") << colorize(MTColor::BLUE, dirs[i].filename().string()) << '\n'; print_tree(dirs[i], prefix + (last?"    ":"│   ")); }
    for (size_t i = 0; i < files.size(); ++i) cout << prefix << ((i+1==files.size())?"└── ":"├── ") << files[i].filename().string() << '\n';
}

static void cmd_tree(const vector<string>& a) {
    string p = "."; if (a.size() > 1) p = a[1]; try { cout << p << '\n'; print_tree(p); } catch (const exception &ex) { eprint_colored(MTColor::RED, string("tree: ") + ex.what() + '\n'); }
}

static void cmd_ps(const vector<string>& a) {
#ifdef _WIN32
    FILE *p = popen("tasklist", "r");
#else
    FILE *p = popen("ps -e -o pid,comm,%cpu,%mem", "r");
#endif
    if (!p) { eprint_colored(MTColor::RED, "ps: failed\n"); return; }
    char buf[512]; while (fgets(buf, sizeof(buf), p)) cout << buf; pclose(p);
}

static void cmd_df(const vector<string>& a) {
#ifndef _WIN32
    struct statvfs st;
    if (statvfs("/", &st) == 0) { double total = double(st.f_blocks) * st.f_frsize / (1024*1024*1024); double avail = double(st.f_bavail) * st.f_frsize / (1024*1024*1024); cout << "/ " << fixed << setprecision(1) << total << "G " << avail << "G\n"; }
#else
    DWORD mask = GetLogicalDrives(); for (char d='A'; d<='Z'; ++d) if (mask & (1<<(d-'A'))) cout << d << ":\\\n";
#endif
}

static void cmd_whoami(const vector<string>& a) {
#ifdef _WIN32
    char user[256]; DWORD len = 256; if (GetUserNameA(user, &len)) cout << user << '\n';
#else
    struct passwd *pw = getpwuid(getuid()); if (pw) cout << pw->pw_name << '\n'; else if (const char* u = getenv("USER")) cout << u << '\n';
#endif
}

static void cmd_date(const vector<string>& a) { time_t t = time(nullptr); cout << colorize(MTColor::GRAY, ctime(&t)); }

static void cmd_clear(const vector<string>& a) {
#ifdef _WIN32
    system("cls");
#else
    cout << "\033[2J\033[H";
#endif
}

static void cmd_echo(const vector<string>& a) {
    for (size_t i = 1; i < a.size(); ++i) { if (i > 1) cout << ' '; cout << a[i]; } cout << '\n';
}

static void cmd_grep(const vector<string>& a) {
    if (a.size() < 3) { eprint_colored(MTColor::YELLOW, "grep: usage grep <pattern> <file>\n"); return; }
    ifstream f(a[2]); if (!f) { eprint_colored(MTColor::RED, "grep: cannot open file\n"); return; }
    string line; size_t lineno = 1;
    while (getline(f, line)) { if (line.find(a[1]) != string::npos) cout << colorize(MTColor::MAGENTA, to_string(lineno) + ": ") << line << '\n'; ++lineno; }
}

static void cmd_wc(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "wc: missing file\n"); return; }
    ifstream f(a[1]); if (!f) { eprint_colored(MTColor::RED, "wc: cannot open file\n"); return; }
    size_t L=0,W=0,C=0; string line;
    while (getline(f, line)) {
        ++L; C += line.size() + 1;
        istringstream iss(line); string w; while (iss >> w) ++W;
    }
    cout << L << " " << W << " " << C << " " << a[1] << '\n';
}

static void cmd_head(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "head: missing file\n"); return; }
    ifstream f(a[1]); if (!f) { eprint_colored(MTColor::RED, "head: cannot open file\n"); return; }
    string line; int n = 0; while (n < 10 && getline(f, line)) { cout << line << '\n'; ++n; }
}

static void cmd_tail(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "tail: missing file\n"); return; }
    ifstream f(a[1]); if (!f) { eprint_colored(MTColor::RED, "tail: cannot open file\n"); return; }
    vector<string> buf; string line; while (getline(f, line)) { buf.push_back(line); if (buf.size() > 10) buf.erase(buf.begin()); }
    for (auto &l : buf) cout << l << '\n';
}

static void cmd_tailf(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "tail -f: missing file\n"); return; }
    const string fname = a[1];
    try {
        std::ifstream file(fname, std::ios::binary);
        if (!file) { eprint_colored(MTColor::RED, "tail -f: cannot open file\n"); return; }

        file.seekg(0, ios::end);
        std::streamoff pos_off = file.tellg();
        if (pos_off < 0) pos_off = 0;

        std::streamoff start_off = 0;
        if (pos_off > static_cast<std::streamoff>(4096)) start_off = pos_off - static_cast<std::streamoff>(4096);
        file.clear();
        file.seekg(start_off, ios::beg);

        string line;
        while (getline(file, line)) cout << line << '\n';

        while (true) {
            if (!getline(file, line)) {
                this_thread::sleep_for(chrono::milliseconds(200));
                file.clear();
            } else {
                cout << line << '\n';
            }
        }
    } catch (const exception &ex) { eprint_colored(MTColor::RED, string("tail -f: ") + ex.what() + "\n"); }
}

static void cmd_chmod(const vector<string>& a) {
    if (a.size() < 3) { eprint_colored(MTColor::YELLOW, "chmod: usage chmod <octal> <file>\n"); return; }
    string s = a[1];
    if (!s.empty() && s[0] == '0') s = s.substr(1);
    if (s.size() < 3) s = string(3 - s.size(), '0') + s;
    int owner = s[s.size()-3] - '0';
    int group = s[s.size()-2] - '0';
    int other = s[s.size()-1] - '0';
    fs::perms p = fs::perms::none;
    if (owner & 4) p |= fs::perms::owner_read;
    if (owner & 2) p |= fs::perms::owner_write;
    if (owner & 1) p |= fs::perms::owner_exec;
    if (group & 4) p |= fs::perms::group_read;
    if (group & 2) p |= fs::perms::group_write;
    if (group & 1) p |= fs::perms::group_exec;
    if (other & 4) p |= fs::perms::others_read;
    if (other & 2) p |= fs::perms::others_write;
    if (other & 1) p |= fs::perms::others_exec;
    try { fs::permissions(a[2], p, fs::perm_options::replace); }
    catch (const exception &ex) { eprint_colored(MTColor::RED, string("chmod: ") + ex.what() + '\n'); }
}

static void cmd_ln(const vector<string>& a) {
    if (a.size() < 3) { eprint_colored(MTColor::YELLOW, "ln: usage ln <target> <link>\n"); return; }
    try { fs::create_symlink(a[1], a[2]); cout << "symlink created\n"; }
    catch (const exception &ex) { eprint_colored(MTColor::RED, string("ln: ") + ex.what() + '\n'); }
}

static void cmd_du(const vector<string>& a) {
    string p = "."; if (a.size() > 1) p = a[1];
    try {
        uintmax_t total = 0;
        for (auto &e : fs::recursive_directory_iterator(p)) {
            try { if (fs::is_regular_file(e)) total += fs::file_size(e); } catch(...){}
        }
        cout << (total / 1024) << "K\t" << p << '\n';
    } catch (const exception &ex) { eprint_colored(MTColor::RED, string("du: ") + ex.what() + '\n'); }
}

static void cmd_sort(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "sort: missing file\n"); return; }
    ifstream f(a[1]); if (!f) { eprint_colored(MTColor::RED, "sort: cannot open file\n"); return; }
    vector<string> lines; string line; while (getline(f, line)) lines.push_back(line);
    sort(lines.begin(), lines.end()); for (auto &l : lines) cout << l << '\n';
}

static void cmd_uniq(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "uniq: missing file\n"); return; }
    ifstream f(a[1]); if (!f) { eprint_colored(MTColor::RED, "uniq: cannot open file\n"); return; }
    string prev, cur; if (getline(f, prev)) cout << prev << '\n'; while (getline(f, cur)) { if (cur != prev) cout << cur << '\n'; prev = cur; }
}

static void cmd_history(const vector<string>& a) {
    if (a.size() > 1 && a[1] == "-c") { history_buf.clear(); cout << "history cleared\n"; return; }
    for (size_t i = 0; i < history_buf.size(); ++i) cout << i+1 << "  " << history_buf[i] << '\n';
}

// -- EXTRA commands --------------------------------------------------------

static void cmd_which(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "which: missing argument\n"); return; }
    string cmd = a[1];
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    const char* path_env = getenv("PATH");
    if (!path_env) { eprint_colored(MTColor::RED, "which: PATH not set\n"); return; }
    string path = path_env;
    stringstream ss(path);
    string part;
    while (getline(ss, part, sep)) {
        fs::path candidate = fs::path(part) / cmd;
        if (is_executable_file(candidate)) { cout << candidate.string() << '\n'; return; }
#ifdef _WIN32
        for (auto &ext : { ".exe", ".com", ".bat", ".cmd" }) {
            fs::path c2 = candidate; c2 += ext;
            if (is_executable_file(c2)) { cout << c2.string() << '\n'; return; }
        }
#endif
    }
    cout << "which: not found\n";
}

static void cmd_open(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "open: missing file\n"); return; }
    string file = a[1];
#ifdef _WIN32
    string cmd = "start \"\" \"" + file + "\"";
    system(cmd.c_str());
#elif __APPLE__
    string cmd = "open \"" + file + "\" &>/dev/null &";
    system(cmd.c_str());
#else
    string cmd = "xdg-open \"" + file + "\" &>/dev/null &";
    system(cmd.c_str());
#endif
}

static void cmd_env(const vector<string>& a) {
#ifdef _WIN32
    LPWCH env = GetEnvironmentStringsW();
    if (env == NULL) { eprint_colored(MTColor::RED, "env: failed\n"); return; }
    LPWCH cur = env;
    while (*cur) {
        wstring ws(cur);
        string s(ws.begin(), ws.end());
        cout << s << '\n';
        cur += ws.size() + 1;
    }
    FreeEnvironmentStringsW(env);
#else
    extern char **environ;
    for (char **env = environ; *env; ++env) cout << *env << '\n';
#endif
}

static void cmd_setenv(const vector<string>& a) {
    if (a.size() < 3) { eprint_colored(MTColor::YELLOW, "setenv: usage setenv NAME VALUE\n"); return; }
#ifdef _WIN32
    if (!SetEnvironmentVariableA(a[1].c_str(), a[2].c_str())) eprint_colored(MTColor::RED, "setenv: failed\n");
#else
    if (setenv(a[1].c_str(), a[2].c_str(), 1) != 0) eprint_colored(MTColor::RED, "setenv: failed\n");
#endif
}

static void cmd_stat(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "stat: missing file\n"); return; }
    fs::path p(a[1]);
    if (!fs::exists(p)) { eprint_colored(MTColor::YELLOW, "stat: not found\n"); return; }
    try {
        cout << colorize(MTColor::GRAY, "path: ") << p.string() << '\n';
        cout << colorize(MTColor::GRAY, "size: ") << (fs::is_regular_file(p) ? to_string(fs::file_size(p)) : string("-")) << '\n';
        cout << colorize(MTColor::GRAY, "type: ") << (fs::is_directory(p) ? "directory" : (fs::is_regular_file(p) ? "file" : "other")) << '\n';
        cout << colorize(MTColor::GRAY, "perm: ") << perms_to_string(fs::status(p).permissions()) << '\n';
        cout << colorize(MTColor::GRAY, "mtime: ") << file_time_string(fs::last_write_time(p)) << '\n';
    } catch (const exception &ex) { eprint_colored(MTColor::RED, string("stat: ") + ex.what() + '\n'); }
}

static void cmd_count(const vector<string>& a) {
    string p = ".";
    if (a.size() > 1) p = a[1];
    try {
        size_t files = 0, dirs = 0;
        for (auto &e : fs::recursive_directory_iterator(p)) {
            try { if (fs::is_directory(e)) ++dirs; else if (fs::is_regular_file(e)) ++files; } catch(...) {}
        }
        cout << colorize(MTColor::CYAN, "files: ") << files << "    " << colorize(MTColor::CYAN, "dirs: ") << dirs << '\n';
    } catch (const exception &ex) { eprint_colored(MTColor::RED, string("count: ") + ex.what() + '\n'); }
}

static void cmd_alias(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "alias: usage alias name='command'\n"); return; }
    string s = a[1];
    auto pos = s.find('=');
    if (pos == string::npos) { eprint_colored(MTColor::YELLOW, "alias: need name=command\n"); return; }
    string name = s.substr(0, pos);
    string cmd = s.substr(pos+1);
    if (cmd.size() >= 2 && ((cmd.front() == '"' && cmd.back() == '"') || (cmd.front() == '\'' && cmd.back() == '\'')))
        cmd = cmd.substr(1, cmd.size()-2);
    alias_map[name] = cmd;
    cout << "alias " << colorize(MTColor::MINT_GREEN, name) << " -> " << cmd << '\n';
}

static void cmd_unalias(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "unalias: usage unalias name\n"); return; }
    auto it = alias_map.find(a[1]);
    if (it == alias_map.end()) { eprint_colored(MTColor::YELLOW, "unalias: not found\n"); return; }
    alias_map.erase(it);
    cout << "unalias: removed\n";
}

static void cmd_aliases(const vector<string>& a) {
    for (auto &kv : alias_map) cout << colorize(MTColor::MINT_GREEN, kv.first) << "='" << kv.second << "'\n";
}

static void cmd_uptime(const vector<string>& a) {
#ifdef _WIN32
    ULONGLONG ms = GetTickCount64();
    long long secs = (long long)(ms / 1000);
    cout << colorize(MTColor::CYAN, "uptime: ") << secs << " seconds\n";
#else
    ifstream f("/proc/uptime");
    if (f) {
        double up = 0;
        f >> up;
        long long secs = (long long)up;
        cout << colorize(MTColor::CYAN, "uptime: ") << secs << " seconds\n";
        return;
    } else {
        using namespace chrono;
        static auto start = steady_clock::now();
        auto now = steady_clock::now();
        auto secs = duration_cast<seconds>(now - start).count();
        cout << colorize(MTColor::CYAN, "uptime (process): ") << secs << " seconds\n";
        return;
    }
#endif
}

static void cmd_ping(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "ping: missing host\n"); return; }
    string host = a[1];
    string cmd;
#ifdef _WIN32
    int count = 4;
    for (size_t i=2;i+1<a.size();++i) {
        if (a[i] == "-c") count = stoi(a[i+1]);
    }
    cmd = "ping -n " + to_string(count) + " " + host;
#else
    int count = 4;
    for (size_t i=2;i+1<a.size();++i) {
        if (a[i] == "-c") count = stoi(a[i+1]);
    }
    cmd = "ping -c " + to_string(count) + " " + host;
#endif
    system(cmd.c_str());
}

static void cmd_hash(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "hash: missing file\n"); return; }
    string f = a[1];
#ifdef _WIN32
    string cmd = "certutil -hashfile \"" + f + "\" SHA256";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) { eprint_colored(MTColor::RED, "hash: failed to run certutil\n"); return; }
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) cout << buf;
    pclose(p);
#else
    string cmd = "sha256sum \"" + f + "\"";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) { eprint_colored(MTColor::RED, "hash: failed\n"); return; }
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) cout << buf;
    pclose(p);
#endif
}

static void cmd_compress(const vector<string>& a) {
    if (a.size() < 3) { eprint_colored(MTColor::YELLOW, "compress: usage compress <file/dir> <out.zip>\n"); return; }
    string src = a[1], out = a[2];
#ifdef _WIN32
    string cmd = "tar -a -c -f \"" + out + "\" \"" + src + "\"";
    system(cmd.c_str());
#else
    string cmd = "zip -r \"" + out + "\" \"" + src + "\"";
    system(cmd.c_str());
#endif
}

static void cmd_extract(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "extract: usage extract <archive>\n"); return; }
    string ar = a[1];
#ifdef _WIN32
    string cmd = "tar -xf \"" + ar + "\"";
    system(cmd.c_str());
#else
    string cmd = "unzip \"" + ar + "\"";
    if (system(cmd.c_str()) != 0) {
        cmd = "tar -xf \"" + ar + "\"";
        system(cmd.c_str());
    }
#endif
}

// expression evaluator
static const char *expr_ptr;
static double parse_expression();
static double parse_number() {
    while (isspace(*expr_ptr)) ++expr_ptr;
    double sign = 1;
    if (*expr_ptr == '+') { ++expr_ptr; }
    else if (*expr_ptr == '-') { sign = -1; ++expr_ptr; }
    while (isspace(*expr_ptr)) ++expr_ptr;
    if (*expr_ptr == '(') {
        ++expr_ptr;
        double v = parse_expression();
        if (*expr_ptr == ')') ++expr_ptr;
        return sign * v;
    }
    char *end;
    double val = strtod(expr_ptr, &end);
    if (end == expr_ptr) return 0.0;
    expr_ptr = end;
    return sign * val;
}
static double parse_term() {
    double v = parse_number();
    while (true) {
        while (isspace(*expr_ptr)) ++expr_ptr;
        if (*expr_ptr == '*') { ++expr_ptr; v *= parse_number(); }
        else if (*expr_ptr == '/') { ++expr_ptr; v /= parse_number(); }
        else break;
    }
    return v;
}
static double parse_expression() {
    double v = parse_term();
    while (true) {
        while (isspace(*expr_ptr)) ++expr_ptr;
        if (*expr_ptr == '+') { ++expr_ptr; v += parse_term(); }
        else if (*expr_ptr == '-') { ++expr_ptr; v -= parse_term(); }
        else break;
    }
    return v;
}

static void cmd_calc(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "calc: usage calc \"expression\"\n"); return; }
    string expr = a[1];
    expr_ptr = expr.c_str();
    double res = parse_expression();
    cout << colorize(MTColor::ORANGE, to_string(res)) << '\n';
}

static void cmd_random(const vector<string>& a) {
    int minv = 0, maxv = 100, count = 1;
    if (a.size() >= 2) minv = stoi(a[1]);
    if (a.size() >= 3) maxv = stoi(a[2]);
    if (a.size() >= 4) count = stoi(a[3]);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(minv, maxv);
    for (int i=0;i<count;++i) cout << colorize(MTColor::BRIGHT_GREEN, to_string(dist(gen))) << (i+1==count?'\n':' ');
}

// bookmarks + replace/edit/top/net/notify

static void cmd_bookmark(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "bookmark: usage bookmark <name>\n"); return; }
    try {
        string cwd = fs::current_path().string();
        bookmarks[a[1]] = cwd;
        cout << "bookmarked " << colorize(MTColor::MINT_GREEN, a[1]) << " -> " << cwd << '\n';
    } catch (const exception &ex) { eprint_colored(MTColor::RED, string("bookmark: ") + ex.what() + '\n'); }
}

static void cmd_bookmarks(const vector<string>& a) {
    if (bookmarks.empty()) { cout << colorize(MTColor::GRAY, "(no bookmarks)\n"); return; }
    for (auto &kv : bookmarks) cout << colorize(MTColor::MINT_GREEN, kv.first) << " -> " << kv.second << '\n';
}

static void cmd_unbookmark(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "unbookmark: usage unbookmark <name>\n"); return; }
    if (bookmarks.erase(a[1])) cout << "removed\n"; else eprint_colored(MTColor::YELLOW, "unbookmark: not found\n");
}

static void cmd_goto(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "goto: usage goto <name>\n"); return; }
    auto it = bookmarks.find(a[1]);
    if (it == bookmarks.end()) { eprint_colored(MTColor::YELLOW, "goto: not found\n"); return; }
    try { fs::current_path(it->second); cout << "cwd -> " << colorize(MTColor::MINT_GREEN, it->second) << '\n'; }
    catch (const exception &ex) { eprint_colored(MTColor::RED, string("goto: ") + ex.what() + '\n'); }
}

static void cmd_replace(const vector<string>& a) {
    if (a.size() < 4) { eprint_colored(MTColor::YELLOW, "replace: usage replace <file> <old> <new>\n"); return; }
    string file = a[1], oldv = a[2], newv = a[3];
    try {
        ifstream in(file);
        if (!in) { eprint_colored(MTColor::RED, "replace: cannot open file\n"); return; }
        string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
        in.close();
        string bak = file + ".bak";
        ofstream outbak(bak, ios::binary);
        outbak << content;
        outbak.close();
        size_t pos = 0;
        while ((pos = content.find(oldv, pos)) != string::npos) {
            content.replace(pos, oldv.size(), newv);
            pos += newv.size();
        }
        ofstream out(file, ios::binary);
        out << content;
        out.close();
        cout << "replaced (backup -> " << bak << ")\n";
    } catch (const exception &ex) { eprint_colored(MTColor::RED, string("replace: ") + ex.what() + '\n'); }
}

static void cmd_top(const vector<string>& a) {
#ifdef _WIN32
    system("taskmgr");
#else
    if (system("command -v htop >/dev/null 2>&1") == 0) system("htop");
    else system("top");
#endif
}

static void cmd_net(const vector<string>& a) {
#ifdef _WIN32
    system("ipconfig /all");
#else
    if (system("command -v ip >/dev/null 2>&1") == 0) system("ip addr");
    else system("ifconfig -a");
#endif
}

static void cmd_notify(const vector<string>& a) {
    if (a.size() < 2) { eprint_colored(MTColor::YELLOW, "notify: usage notify <message>\n"); return; }
    string msg = a[1];
#ifdef _WIN32
    cout << "[notify] " << msg << '\n';
#else
    string cmd = "notify-send \"mintterm\" \"" + msg + "\"";
    system(cmd.c_str());
#endif
}

// alias substitution
static string substitute_aliases(const string &line) {
    auto args = split_args(line);
    if (args.empty()) return line;
    auto it = alias_map.find(args[0]);
    if (it == alias_map.end()) return line;
    string rest;
    if (args.size() > 1) {
        size_t pos = line.find_first_of(" \t");
        if (pos != string::npos) rest = line.substr(pos+1);
    }
    string replaced = it->second + (rest.empty() ? "" : " " + rest);
    return replaced;
}

// -- main loop ---------------------------------------------------------
int main() {
    enable_ansi_on_windows();
    cout << colorize(MTColor::MINT_GREEN, "Tiny Minty Terminal") << " (" << PLATFORM << ") - type 'help'\n";
    string line;
    while (true) {
        try {
            string path = fs::current_path().string();
            const char* user = getenv("USER");
            string userstr = user ? user : "user";
            char hostbuf[256] = {0};
#ifdef _WIN32
            DWORD len = sizeof(hostbuf);
            if (!GetComputerNameA(hostbuf, &len)) hostbuf[0] = '\0';
#else
            if (gethostname(hostbuf, sizeof(hostbuf)) != 0) hostbuf[0] = '\0';
#endif
            cout << colorize(MTColor::MINT_GREEN, userstr + "@" + string(hostbuf)) << ":" << colorize(MTColor::CYAN, path)
                 << " " << mt_code(MTColor::BOLD) << colorize(MTColor::BRIGHT_GREEN, "> ") << mt_code(MTColor::RESET);
        } catch(...) {
            cout << colorize(MTColor::BRIGHT_GREEN, "> ");
        }

        if (!getline(cin, line)) break;
        if (line.empty()) continue;
        line = substitute_aliases(line);
        history_buf.push_back(line);

        auto args = split_args(line);
        if (args.empty()) continue;
        const string &cmd = args[0];

        if (cmd == "exit" || cmd == "quit") break;
        else if (cmd == "help") cmd_help();
        else if (cmd == "ls") cmd_ls(args);
        else if (cmd == "pwd") cmd_pwd();
        else if (cmd == "cd") cmd_cd(args);
        else if (cmd == "cat") cmd_cat(args);
        else if (cmd == "edit") cmd_edit(args);
        else if (cmd == "mkdir") cmd_mkdir(args);
        else if (cmd == "rm") cmd_rm(args);
        else if (cmd == "rmdir") cmd_rmdir(args);
        else if (cmd == "touch") cmd_touch(args);
        else if (cmd == "cp") cmd_cp(args);
        else if (cmd == "mv") cmd_mv(args);
        else if (cmd == "find") cmd_find(args);
        else if (cmd == "tree") cmd_tree(args);
        else if (cmd == "ps") cmd_ps(args);
        else if (cmd == "df") cmd_df(args);
        else if (cmd == "whoami") cmd_whoami(args);
        else if (cmd == "date") cmd_date(args);
        else if (cmd == "clear") cmd_clear(args);
        else if (cmd == "echo") cmd_echo(args);
        else if (cmd == "grep") cmd_grep(args);
        else if (cmd == "wc") cmd_wc(args);
        else if (cmd == "head") cmd_head(args);
        else if (cmd == "tail") {
            if (args.size() > 1 && args[1] == "-f") cmd_tailf(args);
            else cmd_tail(args);
        }
        else if (cmd == "chmod") cmd_chmod(args);
        else if (cmd == "ln") cmd_ln(args);
        else if (cmd == "du") cmd_du(args);
        else if (cmd == "sort") cmd_sort(args);
        else if (cmd == "uniq") cmd_uniq(args);
        else if (cmd == "history") cmd_history(args);
        else if (cmd == "which") cmd_which(args);
        else if (cmd == "open") cmd_open(args);
        else if (cmd == "env") cmd_env(args);
        else if (cmd == "setenv") cmd_setenv(args);
        else if (cmd == "stat") cmd_stat(args);
        else if (cmd == "count") cmd_count(args);
        else if (cmd == "alias") cmd_alias(args);
        else if (cmd == "unalias") cmd_unalias(args);
        else if (cmd == "aliases") cmd_aliases(args);
        else if (cmd == "uptime") cmd_uptime(args);
        else if (cmd == "ping") cmd_ping(args);
        else if (cmd == "hash") cmd_hash(args);
        else if (cmd == "compress") cmd_compress(args);
        else if (cmd == "extract") cmd_extract(args);
        else if (cmd == "calc") cmd_calc(args);
        else if (cmd == "random") cmd_random(args);
        else if (cmd == "bookmark") cmd_bookmark(args);
        else if (cmd == "bookmarks") cmd_bookmarks(args);
        else if (cmd == "unbookmark") cmd_unbookmark(args);
        else if (cmd == "goto") cmd_goto(args);
        else if (cmd == "replace") cmd_replace(args);
        else if (cmd == "top") cmd_top(args);
        else if (cmd == "net") cmd_net(args);
        else if (cmd == "notify") cmd_notify(args);
        else {
            FILE *p = popen(line.c_str(), "r");
            if (!p) { eprint_colored(MTColor::RED, string("failed to run: ") + line + '\n'); continue; }
            char buf[512];
            while (fgets(buf, sizeof(buf), p)) cout << buf;
            pclose(p);
        }
    }

    cout << colorize(MTColor::GRAY, "Bye\n");
    return 0;
}
