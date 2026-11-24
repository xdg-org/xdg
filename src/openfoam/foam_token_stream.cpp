#include "xdg/openfoam/foam_token_stream.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <utility>

#include "xdg/openfoam/foam_error.h"

namespace xdg {

namespace {
//! Characters that always terminate a token and stand alone as one
constexpr const char* FOAM_PUNCT = "(){}<>;";
} // namespace

FoamTokenStream::FoamTokenStream(const std::string& filepath)
  : origin_(filepath)
{
  std::ifstream in(filepath, std::ios::binary);
  if (!in) {
    foam_fatal_error("Could not open OpenFOAM file {}", filepath);
  }

  in.seekg(0, std::ios::end);
  const std::streampos size = in.tellg();
  if (size < 0) {
    foam_fatal_error("Could not determine the size of {}", filepath);
  }
  in.seekg(0, std::ios::beg);

  buffer_.resize(static_cast<size_t>(size));
  if (size > 0) {
    in.read(&buffer_[0], size);
    if (in.gcount() != size) {
      foam_fatal_error("Short read on {}", filepath);
    }
  }
}

FoamTokenStream::FoamTokenStream(std::string buffer, std::string origin)
  : buffer_(std::move(buffer)), origin_(std::move(origin))
{}

bool FoamTokenStream::is_punct(char c)
{
  for (const char* p = FOAM_PUNCT; *p != '\0'; ++p) {
    if (c == *p) return true;
  }
  return false;
}

void FoamTokenStream::skip_ws()
{
  while (pos_ < buffer_.size()) {
    const char c = buffer_[pos_];
    if (std::isspace(static_cast<unsigned char>(c))) {
      pos_++;
    } else if (c == '/' && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == '/') {
      while (pos_ < buffer_.size() && buffer_[pos_] != '\n') pos_++;
    } else if (c == '/' && pos_ + 1 < buffer_.size() && buffer_[pos_ + 1] == '*') {
      pos_ += 2;
      while (pos_ + 1 < buffer_.size() &&
             !(buffer_[pos_] == '*' && buffer_[pos_ + 1] == '/')) {
        pos_++;
      }
      if (pos_ + 1 >= buffer_.size()) error("Unterminated block comment");
      pos_ += 2;
    } else {
      return;
    }
  }
}

bool FoamTokenStream::eof()
{
  skip_ws();
  return pos_ >= buffer_.size();
}

std::string FoamTokenStream::next()
{
  skip_ws();
  if (pos_ >= buffer_.size()) error("Unexpected end of file");

  const char c = buffer_[pos_];

  // quoted string: returned without its delimiters, since OpenFOAM uses quotes
  // to protect values such as arch and note that contain separators
  if (c == '"') {
    const size_t start = ++pos_;
    while (pos_ < buffer_.size() && buffer_[pos_] != '"') pos_++;
    if (pos_ >= buffer_.size()) error("Unterminated string");
    std::string value = buffer_.substr(start, pos_ - start);
    pos_++; // closing quote
    return value;
  }

  if (is_punct(c)) {
    pos_++;
    return std::string(1, c);
  }

  const size_t start = pos_;
  while (pos_ < buffer_.size()) {
    const char d = buffer_[pos_];
    if (std::isspace(static_cast<unsigned char>(d)) || is_punct(d) || d == '"') break;
    pos_++;
  }
  return buffer_.substr(start, pos_ - start);
}

std::string FoamTokenStream::peek()
{
  const size_t saved = pos_;
  std::string token = next();
  pos_ = saved;
  return token;
}

bool FoamTokenStream::at_char(char c)
{
  skip_ws();
  return pos_ < buffer_.size() && buffer_[pos_] == c;
}

void FoamTokenStream::expect(const std::string& expected)
{
  const std::string token = next();
  if (token != expected) {
    error(fmt::format("Expected '{}', found '{}'", expected, token));
  }
}

bool FoamTokenStream::consume_if(const std::string& token)
{
  const size_t saved = pos_;
  if (next() == token) return true;
  pos_ = saved;
  return false;
}

int64_t FoamTokenStream::next_label()
{
  const std::string token = next();
  char* end = nullptr;
  const long long value = std::strtoll(token.c_str(), &end, 10);
  if (end == token.c_str() || *end != '\0') {
    error(fmt::format("Expected an integer, found '{}'", token));
  }
  return static_cast<int64_t>(value);
}

double FoamTokenStream::next_scalar()
{
  const std::string token = next();
  char* end = nullptr;
  const double value = std::strtod(token.c_str(), &end);
  if (end == token.c_str() || *end != '\0') {
    error(fmt::format("Expected a number, found '{}'", token));
  }
  return value;
}

void FoamTokenStream::open_bracket()
{
  skip_ws();
  if (pos_ >= buffer_.size() || buffer_[pos_] != '(') {
    error("Expected '(' to open a list");
  }
  // deliberately no trailing skip: in binary files the payload starts here
  pos_++;
}

void FoamTokenStream::close_bracket()
{
  expect(")");
}

const char* FoamTokenStream::raw(size_t n)
{
  if (pos_ + n > buffer_.size()) {
    error(fmt::format("Unexpected end of file reading a {}-byte binary payload", n));
  }
  const char* data = buffer_.data() + pos_;
  pos_ += n;
  return data;
}

void FoamTokenStream::skip_dict_block()
{
  expect("{");
  int depth = 1;
  while (depth > 0) {
    const std::string token = next(); // errors at EOF, so this terminates
    if (token == "{") depth++;
    else if (token == "}") depth--;
  }
}

void FoamTokenStream::skip_to_semicolon()
{
  while (next() != ";") {}
}

void FoamTokenStream::error(const std::string& message) const
{
  size_t line = 1;
  for (size_t i = 0; i < pos_ && i < buffer_.size(); i++) {
    if (buffer_[i] == '\n') line++;
  }
  foam_fatal_error("{}:{}: {}", origin_, line, message);
}

} // namespace xdg
