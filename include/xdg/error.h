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

// A message on its own is used as written; with arguments, the template below
// formats them into it. There is deliberately no exit-code parameter: next to
// that template it would capture every call passing a single int.
void fatal_error(const std::string& message);

void warning(const std::string& message);

template<typename... Args>
void warning(fmt::format_string<Args...> fstring, Args&&... args)
{
  warning(fmt::format(fstring, std::forward<Args>(args)...));
}

template<typename... Args>
void fatal_error(fmt::format_string<Args...> fstring, Args&&... args)
{
  fatal_error(fmt::format(fstring, std::forward<Args>(args)...));
}

#endif // _XDG_ERROR_H
