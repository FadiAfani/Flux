#include "../lexer/token.hpp"
#include "../parser/node.hpp"
#include <memory>
#include <vector>
namespace flux::parser {

struct ParseError {
  std::string message;
  SourceSpan loc;
};

struct ParseResult {
  Program root;
  std::vector<ParseError> errors;
};

class Parser {

private:
  std::vector<flux::Token> tokens_;
  std::vector<ParseError> errors_;
  size_t cursor_ = 0;
  void report_error(SourceSpan loc, std::string message);
  void report_error_with_span(const Token& start, const Token& end, std::string msg);
  void report_error(const Token& start, std::string msg);

public:
  Parser(std::vector<flux::Token> tokens);
  ParseResult parse();
  std::unique_ptr<LiteralExpr> parse_literal();
  std::unique_ptr<UnaryExpr> parse_unary_expr();
  std::unique_ptr<Expr> parse_expr();

  const Token &peek();
  const Token &peek_next();
  void advance();
};
} // namespace flux::parser
