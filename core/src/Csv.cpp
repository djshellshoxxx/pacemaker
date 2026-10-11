// CSV reader/writer (RS-04 sections 3 and 6).
#include "pacemaker/core/Csv.h"
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace pacemaker { namespace core {

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char) s[a])) ++a;
    while (b > a && std::isspace((unsigned char) s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string toLower(const std::string& s)
{
    std::string r(s);
    for (auto& c : r) c = (char) std::tolower((unsigned char) c);
    return r;
}

std::string normaliseKey(const std::string& s)
{
    std::string r;
    for (unsigned char c : s) if (std::isalnum(c)) r += (char) std::tolower(c);
    return r;
}

bool parseDouble(const std::string& in, double& out)
{
    std::string s = trim(in);
    if (s.empty()) return false;
    // Accept a decimal comma when there is no dot (European spreadsheet exports).
    if (s.find('.') == std::string::npos) {
        const size_t c = s.find(',');
        if (c != std::string::npos && s.find(',', c + 1) == std::string::npos) s[c] = '.';
    }
    // Locale-independent: parse manually.
    size_t i = 0;
    bool neg = false;
    if (s[i] == '+' || s[i] == '-') { neg = s[i] == '-'; ++i; }
    double v = 0.0;
    bool digits = false;
    while (i < s.size() && std::isdigit((unsigned char) s[i])) { v = v * 10.0 + (s[i] - '0'); ++i; digits = true; }
    if (i < s.size() && s[i] == '.') {
        ++i;
        double scale = 0.1;
        while (i < s.size() && std::isdigit((unsigned char) s[i])) { v += (s[i] - '0') * scale; scale *= 0.1; ++i; digits = true; }
    }
    if (!digits) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) { eneg = s[i] == '-'; ++i; }
        int e = 0;
        bool ed = false;
        while (i < s.size() && std::isdigit((unsigned char) s[i])) { if (e < 400) e = e * 10 + (s[i] - '0'); ++i; ed = true; }
        if (!ed) return false;
        v *= std::pow(10.0, eneg ? -e : e);
    }
    if (i != s.size() || !std::isfinite(v)) return false;
    out = neg ? -v : v;
    return true;
}

bool parseInt(const std::string& s, int& out)
{
    double d;
    if (!parseDouble(s, d) || d > 2147483647.0 || d < -2147483648.0) return false;
    out = (int) std::lround(d);
    return true;
}

std::vector<CsvRow> parseCsv(const std::string& textIn, char delimiter)
{
    size_t p = 0;
    const std::string& t = textIn;
    if (t.size() >= 3 && (unsigned char) t[0] == 0xEF && (unsigned char) t[1] == 0xBB && (unsigned char) t[2] == 0xBF)
        p = 3;
    if (delimiter == 0) {
        int commas = 0, semis = 0, tabs = 0;
        bool q = false;
        for (size_t i = p; i < t.size() && (q || (t[i] != '\n' && t[i] != '\r')); ++i) {
            if (t[i] == '"') q = !q;
            else if (!q && t[i] == ',') ++commas;
            else if (!q && t[i] == ';') ++semis;
            else if (!q && t[i] == '\t') ++tabs;
        }
        delimiter = ',';
        if (semis > commas && semis >= tabs) delimiter = ';';
        else if (tabs > commas && tabs > semis) delimiter = '\t';
    }
    std::vector<CsvRow> rows;
    CsvRow row;
    std::string field;
    bool inQuotes = false, any = false;
    auto endRow = [&]() {
        row.push_back(field);
        field.clear();
        // Skip fully empty lines.
        if (!(row.size() == 1 && trim(row[0]).empty())) rows.push_back(row);
        row.clear();
        any = false;
    };
    for (; p < t.size(); ++p) {
        const char c = t[p];
        if (inQuotes) {
            if (c == '"') {
                if (p + 1 < t.size() && t[p + 1] == '"') { field += '"'; ++p; }
                else inQuotes = false;
            } else field += c;
            continue;
        }
        if (c == '"') { inQuotes = true; any = true; }
        else if (c == delimiter) { row.push_back(field); field.clear(); any = true; }
        else if (c == '\r') { if (p + 1 < t.size() && t[p + 1] == '\n') ++p; endRow(); }
        else if (c == '\n') endRow();
        else { field += c; any = true; }
    }
    if (any || !field.empty() || !row.empty()) endRow();
    return rows;
}

std::string csvEscape(const std::string& f, char delimiter)
{
    if (f.find_first_of(std::string("\"\r\n") + delimiter) == std::string::npos) return f;
    std::string r = "\"";
    for (char c : f) { if (c == '"') r += '"'; r += c; }
    r += '"';
    return r;
}

std::string csvLine(const CsvRow& row, char delimiter)
{
    std::string r;
    for (size_t i = 0; i < row.size(); ++i) {
        if (i) r += delimiter;
        r += csvEscape(row[i], delimiter);
    }
    return r;
}

int CsvTable::column(const std::string& name) const
{
    const std::string k = normaliseKey(name);
    for (size_t i = 0; i < header.size(); ++i)
        if (normaliseKey(header[i]) == k) return (int) i;
    return -1;
}

int CsvTable::column(std::initializer_list<const char*> aliases) const
{
    for (const char* a : aliases) {
        const int c = column(std::string(a));
        if (c >= 0) return c;
    }
    return -1;
}

std::string CsvTable::field(size_t r, int c) const
{
    if (c < 0 || r >= rows.size() || (size_t) c >= rows[r].size()) return {};
    return trim(rows[r][(size_t) c]);
}

CsvTable parseCsvTable(const std::string& text, char delimiter)
{
    CsvTable t;
    auto rows = parseCsv(text, delimiter);
    if (rows.empty()) return t;
    t.header = rows.front();
    for (auto& h : t.header) h = trim(h);
    t.rows.assign(rows.begin() + 1, rows.end());
    return t;
}

}} // namespace pacemaker::core
