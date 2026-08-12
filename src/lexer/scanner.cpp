#include "flux/lexer/scanner.hpp"

#include <cctype>
#include <utility>

namespace flux::lexer {

Scanner::Scanner(std::string_view source) : source_(source) {}

bool Scanner::is_at_end() const { return cursor_ >= source_.size(); }

char Scanner::peek() const { return is_at_end() ? '\0' : source_[cursor_]; }

char Scanner::peek_next() const {
  return cursor_ + 1 >= source_.size() ? '\0' : source_[cursor_ + 1];
}

char Scanner::advance() {
  const char value = source_[cursor_++];
  if (value == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return value;
}

bool Scanner::match(char expected) {
  if (is_at_end() || peek() != expected) {
    return false;
  }
  advance();
  return true;
}

Token Scanner::init_token(TokenKind kind) const {
  return {.kind = kind,
          .location = {.offset = cursor_, .line = line_, .column = column_}};
}

void Scanner::report(SourceLocation start, std::string message) {
  errors_.push_back({
      .span = {.start = start,
               .end = {.offset = cursor_, .line = line_, .column = column_}},
      .message = std::move(message),
  });
}

void Scanner::skip_block_comment(SourceLocation start) {
  std::size_t depth = 1;
  while (!is_at_end() && depth != 0) {
    if (peek() == '/' && peek_next() == '*') {
      advance();
      advance();
      ++depth;
    } else if (peek() == '*' && peek_next() == '/') {
      advance();
      advance();
      --depth;
    } else {
      advance();
    }
  }
  if (depth != 0) {
    report(start, "Unterminated block comment");
  }
}

void Scanner::skip_trivia() {
  while (!is_at_end()) {
    if (std::isspace(static_cast<unsigned char>(peek()))) {
      advance();
      continue;
    }
    if (peek() == '/' && peek_next() == '/') {
      while (!is_at_end() && peek() != '\n') {
        advance();
      }
      continue;
    }
    if (peek() == '/' && peek_next() == '*') {
      const SourceLocation start{
          .offset = cursor_, .line = line_, .column = column_};
      advance();
      advance();
      skip_block_comment(start);
      continue;
    }
    break;
  }
}

Token Scanner::scan_identifier_or_keyword() {
  Token token = init_token(TokenKind::Identifier);
  while (!is_at_end()) {
    const auto value = static_cast<unsigned char>(peek());
    if (!std::isalnum(value) && peek() != '_' && value < 0x80) {
      break;
    }
    token.lexeme += advance();
  }

  const auto keyword = keywords.find(token.lexeme);
  if (keyword != keywords.end()) {
    token.kind = keyword->second;
  }
  return token;
}

Token Scanner::scan_number() {
  Token token = init_token(TokenKind::IntegerLiteral);

  if (peek() == '0' && (peek_next() == 'x' || peek_next() == 'X' ||
                        peek_next() == 'b' || peek_next() == 'B')) {
    const bool hexadecimal = peek_next() == 'x' || peek_next() == 'X';
    token.lexeme += advance();
    token.lexeme += advance();
    while (!is_at_end()) {
      const auto value = static_cast<unsigned char>(peek());
      if (peek() == '_' || (hexadecimal && std::isxdigit(value)) ||
          (!hexadecimal && (peek() == '0' || peek() == '1'))) {
        token.lexeme += advance();
      } else {
        break;
      }
    }
    return token;
  }

  while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') {
    token.lexeme += advance();
  }

  if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
    token.kind = TokenKind::FloatLiteral;
    token.lexeme += advance();
    while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') {
      token.lexeme += advance();
    }
    if (peek() == 'e' || peek() == 'E') {
      token.lexeme += advance();
      if (peek() == '+' || peek() == '-') {
        token.lexeme += advance();
      }
      while (std::isdigit(static_cast<unsigned char>(peek())) ||
             peek() == '_') {
        token.lexeme += advance();
      }
    }
  }
  return token;
}

Token Scanner::scan_quoted(char quote, TokenKind kind) {
  Token token = init_token(kind);
  const SourceLocation start = token.location;
  advance();

  while (!is_at_end() && peek() != quote) {
    if (peek() == '\n') {
      report(start, kind == TokenKind::StringLiteral
                        ? "Unterminated string literal"
                        : "Unterminated character literal");
      token.kind = TokenKind::Error;
      return token;
    }
    if (peek() == '\\') {
      token.lexeme += advance();
      if (is_at_end()) {
        break;
      }
    }
    token.lexeme += advance();
  }

  if (is_at_end()) {
    report(start, kind == TokenKind::StringLiteral
                      ? "Unterminated string literal"
                      : "Unterminated character literal");
    token.kind = TokenKind::Error;
    return token;
  }

  advance();
  return token;
}

Token Scanner::scan_token() {
  skip_trivia();
  Token token = init_token(TokenKind::Invalid);
  if (is_at_end()) {
    token.kind = TokenKind::EndOfFile;
    return token;
  }

  const auto value = static_cast<unsigned char>(peek());
  if (std::isalpha(value) || peek() == '_' || value >= 0x80) {
    return scan_identifier_or_keyword();
  }
  if (std::isdigit(value)) {
    return scan_number();
  }

  const auto single = [&](TokenKind kind) {
    token.kind = kind;
    token.lexeme += advance();
    return token;
  };

  switch (peek()) {
  case '[':
    return single(TokenKind::LBracket);
  case ']':
    return single(TokenKind::RBracket);
  case '(':
    return single(TokenKind::LParen);
  case ')':
    return single(TokenKind::RParen);
  case '{':
    return single(TokenKind::LBrace);
  case '}':
    return single(TokenKind::RBrace);
  case ',':
    return single(TokenKind::Comma);
  case ':':
    return single(TokenKind::Colon);
  case ';':
    return single(TokenKind::SemiColon);
  case '?':
    return single(TokenKind::Question);
  case '\"':
    return scan_quoted('\"', TokenKind::StringLiteral);
  case '\'':
    return scan_quoted('\'', TokenKind::CharacterLiteral);
  case '.':
    token = single(TokenKind::Dot);
    if (match('.')) {
      token.kind = TokenKind::DotDot;
      token.lexeme += '.';
      if (match('=')) {
        token.kind = TokenKind::DotDotEqual;
        token.lexeme += '=';
      }
    }
    return token;
  case '+':
    token = single(TokenKind::Plus);
    if (match('=')) {
      token.kind = TokenKind::PlusEqual;
      token.lexeme += '=';
    }
    return token;
  case '-':
    token = single(TokenKind::Minus);
    if (match('=')) {
      token.kind = TokenKind::MinusEqual;
      token.lexeme += '=';
    } else if (match('>')) {
      token.kind = TokenKind::Arrow;
      token.lexeme += '>';
    }
    return token;
  case '*':
    token = single(TokenKind::Star);
    if (match('=')) {
      token.kind = TokenKind::StarEqual;
      token.lexeme += '=';
    }
    return token;
  case '/':
    token = single(TokenKind::Slash);
    if (match('=')) {
      token.kind = TokenKind::SlashEqual;
      token.lexeme += '=';
    }
    return token;
  case '%':
    token = single(TokenKind::Percent);
    if (match('=')) {
      token.kind = TokenKind::PercentEqual;
      token.lexeme += '=';
    }
    return token;
  case '=':
    token = single(TokenKind::Equal);
    if (match('=')) {
      token.kind = TokenKind::DoubleEqual;
      token.lexeme += '=';
    } else if (match('>')) {
      token.kind = TokenKind::BigArrow;
      token.lexeme += '>';
    }
    return token;
  case '!':
    token = single(TokenKind::Bang);
    if (match('=')) {
      token.kind = TokenKind::BangEqual;
      token.lexeme += '=';
    }
    return token;
  case '<':
    token = single(TokenKind::Less);
    if (match('=')) {
      token.kind = TokenKind::LessEqual;
      token.lexeme += '=';
      if (match('>')) {
        token.kind = TokenKind::Equivalence;
        token.lexeme += '>';
      }
    }
    return token;
  case '>':
    token = single(TokenKind::Greater);
    if (match('=')) {
      token.kind = TokenKind::GreaterEqual;
      token.lexeme += '=';
    }
    return token;
  case '&':
    token = single(TokenKind::Error);
    if (match('&')) {
      token.kind = TokenKind::LogicalAnd;
      token.lexeme += '&';
    }
    break;
  case '|':
    token = single(TokenKind::Pipe);
    if (match('|')) {
      token.kind = TokenKind::LogicalOr;
      token.lexeme += '|';
    }
    return token;
  default:
    token = single(TokenKind::Error);
    break;
  }

  if (token.kind == TokenKind::Error) {
    report(token.location, "Unexpected character");
  }
  return token;
}

ScanResult Scanner::scan() {
  while (true) {
    Token token = scan_token();
    const TokenKind kind = token.kind;
    tokens_.push_back(std::move(token));
    if (kind == TokenKind::EndOfFile) {
      break;
    }
  }
  return {.tokens = std::move(tokens_), .errors = std::move(errors_)};
}

} // namespace flux::lexer
