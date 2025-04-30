
#include <iostream>
#include <iomanip>

void output(const std::string& message, std::ostream& out, int indent)
{
  // Set line wrapping and indentation
  int line_wrap = 80;

  // Determine length of message
  int length = message.size();

  int i_start = 0;
  int line_len = line_wrap - indent + 1;
  while (i_start < length) {
    if (length - i_start < line_len) {
      // Remainder of message will fit on line
      out << message.substr(i_start) << std::endl;
      break;

    } else {
      // Determine last space in current line
      std::string s = message.substr(i_start, line_len);
      auto pos = s.find_last_of(' ');

      // Write up to last space, or whole line if no space is present
      out << s.substr(0, pos) << '\n' << std::setw(indent) << " ";

      // Advance starting position
      i_start += (pos == std::string::npos) ? line_len : pos + 1;
    }
  }
}

void write_message(const std::string& message)
{
    std::cout << " ";
    output(message, std::cout, 1);
}

[[noreturn]] void fatal_error(const std::string& message, int err)
{
#ifdef _POSIX_VERSION
  // Make output red if user is in a terminal
  if (isatty(STDERR_FILENO)) {
    std::cerr << "\033[0;31m";
  }
#endif

  // Write error message
  std::cerr << " ERROR: ";
  output(message, std::cerr, 8);

#ifdef _POSIX_VERSION
  // Reset color for terminal
  if (isatty(STDERR_FILENO)) {
    std::cerr << "\033[0m";
  }
#endif

#ifdef OPENMC_MPI
  MPI_Abort(mpi::intracomm, err);
#endif

  // Abort the program
  std::exit(err);
}