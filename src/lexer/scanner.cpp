
#include "../../include/flux/lexer/scanner.hpp"
#include <cctype>

namespace flux::lexer {

Scanner::Scanner(std::string_view source) : source_(source) {}

char Scanner::advance() {
    char c = source_[cursor_++];

    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }

    return c;
}
char Scanner::peek() const { return is_at_end() ? '\0' : source_[cursor_]; }
bool Scanner::is_at_end() const { return cursor_ >= source_.length();}
char Scanner::peek_next() const {
  if (cursor_ + 1 >= source_.length())
    return '\0';

  return source_[cursor_ + 1];
}

bool Scanner::match(char c) { return !is_at_end() && c == source_[cursor_]; }

void Scanner::skip_whitespace() {
  while (!is_at_end()) {
    const char c = peek();

    if (c == ' ' || c == '\t' || c == '\r') {
      advance();
      continue;
    }

    if (c == '\n') {
      advance();
      continue;
    }

    break;
  }
}

inline Token Scanner::init_token(TokenKind kind) {
    SourceLocation loc = {.offset = cursor_, .line = line_, .column = column_};

    return {.kind = kind, .location = loc };
}

Token Scanner::scan_number() {
    Token t = init_token(TokenKind::IntegerLiteral);

    while (std::isdigit(static_cast<unsigned char>(peek()))) {
        const char c = peek();
        t.lexeme += c;
        advance();
    }


    if (match('.')) {
        t.kind = TokenKind::FloatLiteral;
        t.lexeme += '.';
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            const char c = peek();
            t.lexeme += c;
            advance();
        }
    }
    return t;
}

Token Scanner::scan_string() {
    Token t = init_token(TokenKind::StringLiteral);
    const SourceLocation start = t.location;

    advance(); // consume opening quote

    while (cursor_ < source_.length()) {
        const char c = peek();

        if (c == '"') {
            advance();
            return t;
        }

        if (c == '\n') {
            errors_.push_back({
                .span = {.start = start, .end = {.offset = cursor_, .line = line_, .column = column_}},
                .message = "Unterminated string literal",
            });
            t.kind = TokenKind::Error;
            return t;
        }

        if (c == '\\') {
            t.lexeme += c;
            advance();

            if (cursor_ >= source_.length()) {
                break;
            }

            const char escaped = peek();
            if (escaped == '\n') {
                errors_.push_back({
                    .span = {.start = start, .end = {.offset = cursor_, .line = line_, .column = column_}},
                    .message = "Unterminated string literal",
                });
                t.kind = TokenKind::Error;
                return t;
            }

            t.lexeme += escaped;
            advance();
            continue;
        }

        t.lexeme += c;
        advance();
    }

    errors_.push_back({
        .span = {.start = start, .end = {.offset = cursor_, .line = line_, .column = column_}},
        .message = "Unterminated string literal",
    });
    t.kind = TokenKind::Error;
    return t;
}

Token Scanner::scan_identifier_or_keyword() {
    Token t = init_token(TokenKind::Identifier);

    while (cursor_ < source_.length()) {
        const char c = peek();
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            break;
        }

        t.lexeme += c;
        advance();
    }

    const auto keyword = keywords.find(t.lexeme);
    if (keyword != keywords.end()) {
        t.kind = keyword->second;
    }

    return t;
}

Token Scanner::scan_token() {
    skip_whitespace();

    Token t = init_token(TokenKind::Invalid);

    if (is_at_end()) {
        t.kind = TokenKind::EndOfFile;
        return t;
    }

    const char c = peek();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return scan_identifier_or_keyword();
    }

    if (std::isdigit(static_cast<unsigned char>(c))) {
        return scan_number();
    }

    switch (c) {
        case '[':
            t.kind = TokenKind::LBracket;
            t.lexeme += advance();
            break;
        case ']':
            t.kind = TokenKind::RBracket;
            t.lexeme += advance();
            break;
        case '(':
            t.kind = TokenKind::LParen;
            t.lexeme += advance();
            break;
        case ')':
            t.kind = TokenKind::RParen;
            t.lexeme += advance();
            break;
        case '{':
            t.kind = TokenKind::LBrace;
            t.lexeme += advance();
            break;
        case '}':
            t.kind = TokenKind::RBrace;
            t.lexeme += advance();
            break;
        case ',':
            t.kind = TokenKind::Comma;
            t.lexeme += advance();
            break;
        case '.':
            t.kind = TokenKind::Dot;
            t.lexeme += advance();
            break;
        case ':':
            t.kind = TokenKind::Colon;
            t.lexeme += advance();
            break;
        case ';':
            t.kind = TokenKind::SemiColon;
            t.lexeme += advance();
            break;
        case '|':
            t.kind = TokenKind::Pipe;
            t.lexeme += advance();
            break;
        case '+':
            t.kind = TokenKind::Plus;
            t.lexeme += advance();
            break;
        case '*':
            t.kind = TokenKind::Star;
            t.lexeme += advance();
            break;
        case '/':
            t.kind = TokenKind::Slash;
            t.lexeme += advance();
            break;
        case '"':
            return scan_string();
        case '-':
            t.kind = TokenKind::Minus;
            t.lexeme += advance();
            if (match('>')) {
                t.kind = TokenKind::Arrow;
                t.lexeme += advance();
            }
            break;
        case '=':
            t.kind = TokenKind::Equal;
            t.lexeme += advance();
            if (match('=')) {
                t.kind = TokenKind::DoubleEqual;
                t.lexeme += advance();
            } else if (match('>')) {
                t.kind = TokenKind::BigArrow;
                t.lexeme += advance();
            }
            break;
        case '!':
            t.kind = TokenKind::Bang;
            t.lexeme += advance();
            if (match('=')) {
                t.kind = TokenKind::BangEqual;
                t.lexeme += advance();
            }
            break;
        case '<':
            t.kind = TokenKind::Less;
            t.lexeme += advance();
            if (match('=')) {
                t.kind = TokenKind::LessEqual;
                t.lexeme += advance();
            }
            break;
        case '>':
            t.kind = TokenKind::Greater;
            t.lexeme += advance();
            if (match('=')) {
                t.kind = TokenKind::GreaterEqual;
                t.lexeme += advance();
            }
            break;
        default:
            t.kind = TokenKind::Error;
            t.lexeme += advance();
            errors_.push_back({
                .span = {.start = t.location, .end = {.offset = cursor_, .line = line_, .column = column_}},
                .message = "Unexpected character",
            });
            break;
    }

    return t;
}

ScanResult Scanner::scan() {
    while (true) {
        Token t = scan_token();
        const TokenKind kind = t.kind;
        tokens_.push_back(std::move(t));

        if (kind == TokenKind::EndOfFile) {
            break;
        }
    }

    return {.tokens = std::move(tokens_), .errors = std::move(errors_)};
}

} // namespace flux::lexer
