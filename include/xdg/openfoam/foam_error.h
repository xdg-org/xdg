#ifndef _XDG_OPENFOAM_FOAM_ERROR
#define _XDG_OPENFOAM_FOAM_ERROR

#include <stdexcept>
#include <string>

#include "xdg/error.h"

namespace xdg {

//! \brief Report an error the OpenFOAM backend cannot continue past
//!
//! XDG's fatal_error returns when the application has chosen
//! ErrorPolicy::NONE. Nothing in this backend can carry on from a malformed
//! mesh or an invalid ID, so if fatal_error does return, this throws instead.
[[noreturn]] inline void foam_fatal_error(const std::string& message)
{
  fatal_error(message);
  throw std::runtime_error(message);
}

//! \brief Format the message, then report it as above
template<typename... Params>
[[noreturn]] void foam_fatal_error(const std::string& message, const Params&... fmt_args)
{
  foam_fatal_error(fmt::format(message, fmt_args...));
}

} // namespace xdg

#endif // include guard
