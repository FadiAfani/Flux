#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "flux/lexer/scanner.hpp"
#include "flux/parser/parser.hpp"

using flux::SourceLocation;
using flux::Token;
using flux::TokenKind;
using flux::lexer::Scanner;
using flux::parser::BumpAllocator;
using flux::parser::Expr;
using flux::parser::ExternalFunctionDeclaration;
using flux::parser::FunctionDeclaration;
using flux::parser::LiteralExpression;
using flux::parser::ModuleDeclaration;
using flux::parser::Parser;
using flux::parser::ParseResult;
using flux::parser::TypeAliasDeclaration;
using flux::parser::TypeName;
using flux::parser::UnaryExpression;
using flux::parser::UnionType;

namespace {

[[noreturn]] void fail(std::string_view message) {
  std::cerr << message << '\n';
  std::exit(EXIT_FAILURE);
}

Token token(TokenKind kind, std::string lexeme, std::size_t offset = 0) {
  return {.kind = kind,
          .lexeme = std::move(lexeme),
          .location = SourceLocation{
              .offset = offset, .line = 1, .column = offset + 1}};
}

ParseResult parse_source(std::string_view source) {
  Scanner scanner(source);
  auto scanned = scanner.scan();
  if (!scanned.errors.empty()) {
    fail("integration source did not lex cleanly");
  }
  return Parser(std::move(scanned.tokens)).parse();
}

void creates_literal_in_arena() {
  Parser parser({token(TokenKind::IntegerLiteral, "42"),
                 token(TokenKind::EndOfFile, "", 2)});

  Expr *expression = parser.parse_literal();
  if (expression == nullptr ||
      !std::holds_alternative<LiteralExpression>(expression->value)) {
    fail("parser did not create a literal expression");
  }

  const auto &literal = std::get<LiteralExpression>(expression->value);
  if (literal.value.lexeme != "42") {
    fail("literal expression did not retain its token");
  }
}

void creates_recursive_unary_expression_in_arena() {
  Parser parser({token(TokenKind::Minus, "-"),
                 token(TokenKind::IntegerLiteral, "7", 1),
                 token(TokenKind::EndOfFile, "", 2)});

  Expr *expression = parser.parse_expr();
  if (expression == nullptr ||
      !std::holds_alternative<UnaryExpression>(expression->value)) {
    fail("parser did not create a unary expression");
  }

  const auto &unary = std::get<UnaryExpression>(expression->value);
  if (unary.operand == nullptr ||
      !std::holds_alternative<LiteralExpression>(unary.operand->value)) {
    fail("unary operand was not retained in the arena");
  }
}

void parse_result_retains_arena_lifetime() {
  std::weak_ptr<BumpAllocator> arena;

  {
    ParseResult result = [&arena] {
      Parser parser({token(TokenKind::EndOfFile, "")});
      ParseResult parsed = parser.parse();
      arena = parsed.arena;
      return parsed;
    }();

    if (arena.expired() || result.root == nullptr) {
      fail("parse result did not retain its AST arena");
    }

    result.root->imports.clear();
  }

  if (!arena.expired()) {
    fail("AST arena outlived its parse result");
  }
}

void parses_union_type() {
  Parser parser({token(TokenKind::Identifier, "NotFound"),
                 token(TokenKind::Pipe, "|", 9),
                 token(TokenKind::Identifier, "Forbidden", 10),
                 token(TokenKind::Pipe, "|", 19),
                 token(TokenKind::Identifier, "DatabaseError", 20),
                 token(TokenKind::EndOfFile, "", 33)});

  auto *type = parser.parse_union_type();
  if (type == nullptr || !std::holds_alternative<UnionType>(type->value)) {
    fail("parser did not create a union type");
  }

  const auto &members = std::get<UnionType>(type->value).members;
  if (members.size() != 3) {
    fail("union type did not retain all members");
  }

  for (const auto *member : members) {
    if (member == nullptr || !std::holds_alternative<TypeName>(member->value)) {
      fail("union member was not parsed as a type name");
    }
  }
}

void parses_optional_module_declaration() {
  Parser parser({token(TokenKind::KwModule, "module"),
                 token(TokenKind::Identifier, "booking", 7),
                 token(TokenKind::Dot, ".", 14),
                 token(TokenKind::Identifier, "service", 15),
                 token(TokenKind::SemiColon, ";", 22),
                 token(TokenKind::EndOfFile, "", 23)});

  ModuleDeclaration *module = parser.parse_module_declaration();
  if (module == nullptr || module->name.parts.size() != 2 ||
      module->name.parts[0].lexeme != "booking" ||
      module->name.parts[1].lexeme != "service") {
    fail("parser did not create the expected module declaration");
  }

  Parser parser_without_module({token(TokenKind::EndOfFile, "")});
  if (parser_without_module.parse_module_declaration() != nullptr) {
    fail("optional module parser accepted a missing declaration");
  }
}

void parses_source_file_module_and_imports() {
  Parser parser({token(TokenKind::KwModule, "module"),
                 token(TokenKind::Identifier, "app", 7),
                 token(TokenKind::SemiColon, ";", 10),
                 token(TokenKind::KwImport, "import", 12),
                 token(TokenKind::Identifier, "std", 19),
                 token(TokenKind::Dot, ".", 22),
                 token(TokenKind::Identifier, "collections", 23),
                 token(TokenKind::SemiColon, ";", 34),
                 token(TokenKind::EndOfFile, "", 35)});

  ParseResult result = parser.parse();
  auto *source = result.root;
  if (source == nullptr || source->module == nullptr ||
      source->imports.size() != 1 ||
      source->imports[0]->name.parts.size() != 2) {
    fail("source file did not retain its module and imports");
  }
}

void parses_external_function_declaration_with_token_kinds() {
  Parser parser(
      {token(TokenKind::KwPub, "pub"),
       token(TokenKind::KwTrusted, "trusted", 4),
       token(TokenKind::KwExternal, "external", 12),
       token(TokenKind::KwFn, "fn", 21),
       token(TokenKind::Identifier, "os_read", 24),
       token(TokenKind::LParen, "(", 31),
       token(TokenKind::Identifier, "buffer", 32),
       token(TokenKind::Colon, ":", 38), token(TokenKind::KwMut, "mut", 40),
       token(TokenKind::Identifier, "Buffer", 44),
       token(TokenKind::RParen, ")", 50), token(TokenKind::Arrow, "->", 52),
       token(TokenKind::Identifier, "UInt64", 55),
       token(TokenKind::SemiColon, ";", 61),
       token(TokenKind::EndOfFile, "", 62)});

  ExternalFunctionDeclaration *declaration =
      parser.parse_external_function_declaration();
  if (declaration == nullptr || !declaration->is_public ||
      !declaration->is_trusted ||
      declaration->signature.name.lexeme != "os_read" ||
      declaration->signature.parameters.size() != 1 ||
      !declaration->signature.parameters[0].is_mutable ||
      declaration->signature.return_type == nullptr) {
    fail("parser did not create the expected external function declaration");
  }
}

void parses_function_declaration_with_shared_signature() {
  Parser parser(
      {token(TokenKind::KwPub, "pub"), token(TokenKind::KwTotal, "total", 4),
       token(TokenKind::KwFn, "fn", 10),
       token(TokenKind::Identifier, "answer", 13),
       token(TokenKind::LParen, "(", 19), token(TokenKind::RParen, ")", 20),
       token(TokenKind::Arrow, "->", 22),
       token(TokenKind::Identifier, "Int", 25),
       token(TokenKind::LBrace, "{", 29),
       token(TokenKind::IntegerLiteral, "42", 31),
       token(TokenKind::RBrace, "}", 34), token(TokenKind::EndOfFile, "", 35)});

  FunctionDeclaration *declaration = parser.parse_function_declaration();
  if (declaration == nullptr || !declaration->is_public ||
      !declaration->is_total ||
      declaration->signature.name.lexeme != "answer" ||
      declaration->signature.return_type == nullptr ||
      declaration->body == nullptr ||
      declaration->body->tail_expression == nullptr) {
    fail("parser did not create the expected function declaration");
  }
}

void rejects_failed_generic_parameter_in_type_declaration() {
  Parser parser(
      {token(TokenKind::KwType, "type"), token(TokenKind::Identifier, "Box", 5),
       token(TokenKind::Less, "<", 8), token(TokenKind::Identifier, "T", 9),
       token(TokenKind::Comma, ",", 10), token(TokenKind::Greater, ">", 11),
       token(TokenKind::Equal, "=", 13),
       token(TokenKind::Identifier, "Int", 15),
       token(TokenKind::SemiColon, ";", 18),
       token(TokenKind::EndOfFile, "", 19)});

  if (parser.parse_type_declaration().has_value()) {
    fail("type declaration accepted a failed generic parameter");
  }
}

void dispatches_type_alias_declaration() {
  Parser parser(
      {token(TokenKind::KwPub, "pub"), token(TokenKind::KwType, "type", 4),
       token(TokenKind::Identifier, "Box", 9), token(TokenKind::Less, "<", 12),
       token(TokenKind::Identifier, "T", 13),
       token(TokenKind::Greater, ">", 14), token(TokenKind::Equal, "=", 16),
       token(TokenKind::Identifier, "T", 18),
       token(TokenKind::SemiColon, ";", 19),
       token(TokenKind::EndOfFile, "", 20)});

  auto result = parser.parse_type_declaration();
  if (!result.has_value() ||
      !std::holds_alternative<TypeAliasDeclaration *>(result.value())) {
    fail("type declaration did not dispatch to the alias parser");
  }

  auto *alias = std::get<TypeAliasDeclaration *>(result.value());
  if (alias == nullptr || !alias->is_public || alias->name.lexeme != "Box" ||
      alias->generic_parameters.size() != 1 || alias->type == nullptr) {
    fail("type alias parser did not retain the parsed declaration");
  }
}

void parses_representative_grammar_end_to_end() {
  constexpr std::string_view source = R"(
module app.core;
import std.collections.{List, Map as Dictionary};
import std.io as io;

pub const Limit: Int = 10;
type Positive = Int where self > 0;
type NamedRow = { name: String | R };
pub type User { pub id: Int; invariant Valid: id >= 0; }
type Option<T> = Some(T) | None;

effect Database { capability Read; capability Write; }
trusted external fn load(id: Int) -> User uses Database.Read;

trait Eq<T: Type> {
  fn eq(a: T, b: T) -> Bool uses Database.Read;
  law reflexive: forall x: T { eq(x, x) == eq(x, x) };
}

impl Eq<User> {
  fn eq(a: User, b: User) -> Bool { a.id == b.id }
}

domain Rules { invariant Always: true; }

pub fn run<T: Type, F: (Type -> Type)>(items: List<T>) -> Int
  requires Limit > 0;
  ensures result >= 0;
  ensures old(result) <= result;
  uses Database.Read;
{
  let first = items[0]?;
  let factory = identity<T>;
  var count: Int = 0;
  while count < Limit { count += 1; }
  for item in items { consume(item); }
  mutate first { first.id = 2; }
  transaction db { save(first)?; }
  parallel { consume(first); }
  unsafe { raw(first); }
  let user = User<Int> { id: count };
  let changed = user with { id = count + 1 };
  if valid(User<Int> { id: count }) { consume(first); };
  let value = match Some(count) {
    Some(x) if x > 0 => x,
    None => 0,
  };
  value
}
)";

  ParseResult result = parse_source(source);
  if (result.root == nullptr || !result.errors.empty()) {
    if (!result.errors.empty()) {
      std::cerr << "unexpected parser error: " << result.errors.front().message
                << '\n';
    }
    fail("representative grammar source did not parse cleanly");
  }
  if (result.root->module == nullptr || result.root->imports.size() != 2 ||
      result.root->declarations.size() != 11) {
    fail("representative grammar source lost declarations");
  }
}

void recovers_at_the_next_top_level_declaration() {
  ParseResult result =
      parse_source("const broken = ; pub const recovered: Int = 1;");
  if (result.errors.empty() || result.root == nullptr ||
      result.root->declarations.size() != 1) {
    fail("parser did not recover after a malformed declaration");
  }
}

} // namespace

int main() {
  creates_literal_in_arena();
  creates_recursive_unary_expression_in_arena();
  parse_result_retains_arena_lifetime();
  parses_union_type();
  parses_optional_module_declaration();
  parses_source_file_module_and_imports();
  parses_external_function_declaration_with_token_kinds();
  parses_function_declaration_with_shared_signature();
  rejects_failed_generic_parameter_in_type_declaration();
  dispatches_type_alias_declaration();
  parses_representative_grammar_end_to_end();
  recovers_at_the_next_top_level_declaration();
  return EXIT_SUCCESS;
}
