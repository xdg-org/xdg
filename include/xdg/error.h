#ifndef _XDG_ERROR_H
#define _XDG_ERROR_H

#include <string>
#include <utility>

#include <fmt/format.h>

// Borrowed from OpenMC

void write_message(const std::string& message);

template<typename... Args>
void write_message(fmt::format_string<Args...> fstring, Args&&... args)
{
  write_message(fmt::format(fstring, std::forward<Args>(args)...));
}

void fatal_error(const std::string& message, int err=-1);

void warning(const std::string& message);

template<typename... Args>
void warning(fmt::format_string<Args...> fstring, Args&&... args)
{
  warning(fmt::format(fstring, std::forward<Args>(args)...));
}

template<typename... Args>
[[noreturn]] void fatal_error(fmt::format_string<Args...> fstring, Args&&... args)
{
  fatal_error(fmt::format(fstring, std::forward<Args>(args)...));
}

#endif // _XDG_ERROR_H
