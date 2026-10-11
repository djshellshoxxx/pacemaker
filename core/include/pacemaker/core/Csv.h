// Small RFC 4180-style CSV reader/writer for song maps and drift logs (RS-01 sections 6-7, RS-04 sections 3, 6).
// Tolerant: quoted fields, doubled quotes, CRLF, BOM, ';' or tab delimiter auto-detect, ragged rows.
#pragma once
#include <initializer_list>
#include <string>
#include <vector>

namespace pacemaker { namespace core {

using CsvRow = std::vector<std::string>;

// Splits text into rows. delimiter 0 = auto-detect from the first line (',' ';' or '\t').
std::vector<CsvRow> parseCsv(const std::string& text, char delimiter = 0);

// Quotes a field when it contains the delimiter, quotes, or line breaks.
std::string csvEscape(const std::string& field, char delimiter = ',');
std::string csvLine(const CsvRow& row, char delimiter = ','); // no trailing newline

// Table with a header row; column lookup is case-insensitive and ignores spaces, '_' and '-'.
struct CsvTable {
    CsvRow header;
    std::vector<CsvRow> rows;
    int column(const std::string& name) const;                 // -1 when absent
    int column(std::initializer_list<const char*> aliases) const; // first alias found
    // Field of row r in column c, trimmed; empty when missing.
    std::string field(size_t r, int c) const;
};
CsvTable parseCsvTable(const std::string& text, char delimiter = 0);

// Helpers shared by importers.
std::string trim(const std::string& s);
std::string toLower(const std::string& s);
std::string normaliseKey(const std::string& s); // lower case, alnum only
bool parseDouble(const std::string& s, double& out); // whole-string, locale-free '.' or ',' decimal
bool parseInt(const std::string& s, int& out);

}} // namespace pacemaker::core
