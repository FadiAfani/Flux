#pragma once

#include "../lexer/token.hpp"
#include "allocator.hpp"
#include "node.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace flux::parser {

struct ParseError {
  std::string message;
  SourceSpan loc;
};

struct ParseResult {
  std::shared_ptr<BumpAllocator> arena;
  Program *root = nullptr;
  std::vector<ParseError> errors;
};

class Parser {

private:
  std::vector<Token> tokens_;
  std::vector<ParseError> errors_;
  std::shared_ptr<BumpAllocator> arena_;
  std::size_t cursor_ = 0;

  void report_error(SourceSpan loc, std::string message);
  void report_error_with_span(const Token &start, const Token &end,
                              std::string message);
  void report_error(const Token &start, std::string message);

public:
  explicit Parser(std::vector<Token> tokens);

  ParseResult parse();
  Expr *parse_literal();
  Expr *parse_unary_expr();
  Expr *parse_expr();

  const Token &peek();
  const Token &peek_next();
  SourceSpan get_node_span();
  void advance();
};

} // namespace flux::parser
