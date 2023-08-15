#pragma once

#include <iostream>
#include <format>

namespace ks3::detail
{

using namespace std;

template<class ...Args>
void print(FILE *stream, format_string<Args...> fmt, Args &&...args)
{
    // (Args &&)args... = forward<Args>(args)...
    fprintf(stream, format(move(fmt), (Args &&)args...).c_str());
}

template<class ...Args>
void println(FILE *stream, format_string<Args...> fmt, Args &&...args)
{
    // The related function puts appends a newline character to the
    // output, while fputs writes the string unmodified.
    // (Args &&)args... = forward<Args>(args)...
    fputs(format(move(fmt), (Args &&)args...).c_str(), stream);
    fputs("\n", stream);
}

template<class ...Args>
void print(format_string<Args...> fmt, Args &&...args)
{
    // (Args &&)args... = forward<Args>(args)...
    printf(format(move(fmt), (Args &&)args...).c_str());
}

template<class ...Args>
void println(format_string<Args...> fmt, Args &&...args)
{
    // (Args &&)args... = forward<Args>(args)...
    puts(format(move(fmt), (Args &&)args...).c_str());
}

} // namespace ks3::detail
