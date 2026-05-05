#include "../lexer/token.hpp"
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace flux::parser {

struct LiteralExpr {
  Token value;
};

struct UnaryExpr {
  Token op;
  Token value;
};

struct BinaryExpr {
  std::unique_ptr<Token> left;
  Token op;
  std::unique_ptr<Token> right;
};

struct VariableExpr {
  Token name;
};

typedef struct GroupExpr GroupExpr;

using Expr =
    std::variant<LiteralExpr, UnaryExpr, BinaryExpr, VariableExpr, GroupExpr>;

struct GroupExpr {
  std::unique_ptr<Expr> expr;
};

struct IfExpr {
  Expr cond;
  std::vector<Expr> body;
  std::vector<Expr> elifs;
  std::unique_ptr<Expr> else_body;
};

struct AssignStmt {
  Token var;
  Expr value;
};

struct TraitStmt {};

using Stmt = std::variant<AssignStmt, TraitStmt>;

struct Program {
  std::vector<Stmt> stmts;
};

struct BlockExpr {
  std::vector<Stmt> stmts;
  std::unique_ptr<Expr> expr;
};

struct FuncDeclrStmt {
  std::vector<Token> params;
  Token rt;
  BlockExpr expr;
};

struct VarPat {
  Token var;
};

struct TuplePat {
  std::vector<VarPat> vars;
};

struct ListPat {
  std::vector<VarPat> vars;
};

using Pattern = std::variant<VarPat, TuplePat, ListPat>;

/** for variants
 * for [var] in [range]
 */

struct ForStmt {
  Token var;
  Pattern pat;
  Expr expr;
  BlockExpr block;
};

struct WhileStmt {
  Expr cond;
  BlockExpr block;
};

} // namespace flux::parser
