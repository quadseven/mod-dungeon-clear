/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef DC_SOURCE_SCAN_H
#define DC_SOURCE_SCAN_H

// Reading the module's own source from a test: the no-grant checks
// (TestDefiasGunpowder.cpp, TestGrantLedger.cpp) scan src/ for calls that hand
// a player something it could not earn. Header-only, no game headers, so the
// scan builds and runs without a world.

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace DcSourceScan
{
    inline std::filesystem::path ModuleSrc()
    {
        return std::filesystem::path(DC_FIXTURE_DIR) / ".." / ".." / "src";
    }

    inline std::string ReadFile(std::filesystem::path const& p)
    {
        std::ifstream in(p);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Source with comments blanked out (so prose may still name an API) and
    // string literals left alone. Newlines survive, so line numbers hold.
    inline std::string StripComments(std::string const& src)
    {
        std::string out;
        out.reserve(src.size());
        enum { Code, Line, Block, Str, Chr } st = Code;
        for (std::size_t i = 0; i < src.size(); ++i)
        {
            char const c = src[i];
            char const n = i + 1 < src.size() ? src[i + 1] : '\0';
            switch (st)
            {
                case Code:
                    if (c == '/' && n == '/') { st = Line; ++i; continue; }
                    if (c == '/' && n == '*') { st = Block; ++i; continue; }
                    if (c == '"') st = Str;
                    else if (c == '\'') st = Chr;
                    out += c;
                    break;
                case Line:
                    if (c == '\n') { st = Code; out += c; }
                    break;
                case Block:
                    if (c == '*' && n == '/') { st = Code; ++i; }
                    else if (c == '\n') out += c;
                    break;
                case Str:
                case Chr:
                    out += c;
                    if (c == '\\' && i + 1 < src.size()) { out += n; ++i; }
                    else if ((st == Str && c == '"') || (st == Chr && c == '\''))
                        st = Code;
                    break;
            }
        }
        return out;
    }

    inline bool IsSource(std::filesystem::path const& p)
    {
        std::string const ext = p.extension().string();
        return ext == ".cpp" || ext == ".h";
    }

    inline bool IsIdentChar(char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    // Offsets of every CALL of `name` in `code`: the bare identifier (not the
    // tail of a longer one, so TeleportTo does not match NearTeleportTo),
    // followed by optional whitespace and '('.
    inline std::vector<std::size_t> CallOffsets(std::string const& code, std::string const& name)
    {
        std::vector<std::size_t> hits;
        for (std::size_t at = code.find(name); at != std::string::npos;
             at = code.find(name, at + 1))
        {
            if (at > 0 && IsIdentChar(code[at - 1]))
                continue;
            std::size_t k = at + name.size();
            if (k < code.size() && IsIdentChar(code[k]))
                continue;
            while (k < code.size() && std::isspace(static_cast<unsigned char>(code[k])))
                ++k;
            if (k < code.size() && code[k] == '(')
                hits.push_back(at);
        }
        return hits;
    }

    // The top-level arguments of the call whose name starts at `at`, trimmed.
    // Empty when the parentheses do not close.
    inline std::vector<std::string> CallArgs(std::string const& code, std::size_t at)
    {
        std::vector<std::string> args;
        std::size_t open = code.find('(', at);
        if (open == std::string::npos)
            return args;
        int depth = 0;
        std::string cur;
        for (std::size_t i = open; i < code.size(); ++i)
        {
            char const c = code[i];
            if (c == '(' || c == '[' || c == '{')
            {
                if (depth++ > 0)
                    cur += c;
                continue;
            }
            if (c == ')' || c == ']' || c == '}')
            {
                if (--depth == 0)
                {
                    args.push_back(cur);
                    break;
                }
                cur += c;
                continue;
            }
            if (c == ',' && depth == 1)
            {
                args.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        if (depth != 0)
            return {};
        for (std::string& a : args)
        {
            std::size_t const b = a.find_first_not_of(" \t\r\n");
            std::size_t const e = a.find_last_not_of(" \t\r\n");
            a = b == std::string::npos ? std::string() : a.substr(b, e - b + 1);
        }
        return args;
    }

    inline std::size_t LineOf(std::string const& code, std::size_t at)
    {
        std::size_t line = 1;
        for (std::size_t i = 0; i < at && i < code.size(); ++i)
            if (code[i] == '\n')
                ++line;
        return line;
    }
}

#endif
