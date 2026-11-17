#ifndef _XDG_OPENFOAM_FOAM_TOKEN_STREAM
#define _XDG_OPENFOAM_FOAM_TOKEN_STREAM

#include <cstddef>
#include <cstdint>
#include <string>

namespace xdg {

//! \brief Token reader for OpenFOAM's dictionary and list syntax
//!
//! OpenFOAM writes lists in several interchangeable spellings, and more than
//! one may appear in a single mesh: multi-line (`11\n(\n0\n0\n...)`), inline
//! (`1(1)`), typed (`List<label> 1(0)`) and uniform (`10{0}`). Reading is
//! therefore token-based rather than line-based.
//!
//! In binary files the FoamFile header is still ASCII text while the list
//! payloads are raw bytes. Comments are consequently skipped lazily, as tokens
//! are requested, rather than stripped from the buffer up front: a raw payload
//! may legally contain any byte sequence, `//` and `/*` included.
class FoamTokenStream {
public:
  //! \brief Construct a token stream over the contents of a file
  explicit FoamTokenStream(const std::string& filepath);

  //! \brief Construct a token stream over an in-memory buffer
  //! \param buffer The contents to read
  //! \param origin A name used in error messages
  FoamTokenStream(std::string buffer, std::string origin);

  //! \brief Advance past whitespace and comments
  void skip_ws();

  //! \brief Whether the stream is exhausted, ignoring trailing whitespace
  bool eof();

  //! \brief Read the next token, advancing the stream
  std::string next();

  //! \brief Return the next token without advancing the stream
  std::string peek();

  //! \brief Whether the next non-blank character is \p c, without advancing
  //! \note Cheaper than peek() for probing a delimiter, which matters when
  //!       scanning an unsized list of several million entries.
  bool at_char(char c);

  //! \brief Consume the next token, requiring it to equal \p expected
  void expect(const std::string& expected);

  //! \brief Consume the next token if it equals \p token
  //! \return Whether the token was consumed
  bool consume_if(const std::string& token);

  //! \brief Read the next token as an integer
  int64_t next_label();

  //! \brief Read the next token as a floating point value
  double next_scalar();

  //! \brief Consume `(` without skipping the bytes that follow it
  //! \note Binary payloads begin immediately after the delimiter, so no
  //!       whitespace skipping may happen here.
  void open_bracket();

  //! \brief Consume `)`
  void close_bracket();

  //! \brief Consume \p n raw bytes and return a pointer to them
  const char* raw(size_t n);

  //! \brief Number of bytes of the file not yet consumed
  size_t remaining() const { return pos_ < buffer_.size() ? buffer_.size() - pos_ : 0; }

  //! \brief Skip a balanced `{ ... }` block, including any nested blocks
  void skip_dict_block();

  //! \brief Skip tokens through the next `;`
  void skip_to_semicolon();

  //! \brief Report a parse error naming the file and line, and exit
  [[noreturn]] void error(const std::string& message) const;

  const std::string& origin() const { return origin_; }

private:
  static bool is_punct(char c);

  std::string buffer_;
  std::string origin_;
  size_t pos_ {0};
};

} // namespace xdg

#endif // include guard
