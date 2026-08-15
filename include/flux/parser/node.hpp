#pragma once

#include "../lexer/token.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace flux::parser {

struct Node {
  SourceSpan span;
};

struct TypeExpression;
struct KindExpression;
struct Expression;
struct Statement;
struct Block;
struct Pattern;
struct Predicate;
struct SpecificationExpression;

using TypePtr = TypeExpression *;
using KindPtr = KindExpression *;
using ExprPtr = Expression *;
using StmtPtr = Statement *;
using BlockPtr = Block *;
using PatternPtr = Pattern *;
using PredicatePtr = Predicate *;
using SpecExprPtr = SpecificationExpression *;

struct QualifiedName {
  std::vector<Token> parts;
};

struct ModuleDeclaration : Node {
  QualifiedName name;
};

struct ImportItem {
  Token name;
  std::optional<Token> alias;
};

struct ImportAll {};

struct ImportAlias {
  Token alias;
};

struct ImportItems {
  std::vector<ImportItem> items;
};

using ImportSelector = std::variant<ImportAll, ImportAlias, ImportItems>;

struct ImportDeclaration : Node {
  QualifiedName name;
  std::optional<ImportSelector> selector;
};

struct MetaArgument {
  std::variant<TypePtr, ExprPtr> value;
};

struct EffectReference {

  QualifiedName name;
  std::vector<MetaArgument> arguments;
};

struct TypeName {
  QualifiedName name;
};

struct UnionType {
  std::vector<TypePtr> members;
};

struct FunctionType {
  std::vector<TypePtr> parameters;
  TypePtr result = nullptr;
  std::vector<EffectReference> effects;
};

struct AppliedType {
  TypePtr base = nullptr;
  std::vector<MetaArgument> arguments;
};

struct TupleType {
  std::vector<TypePtr> elements;
};

struct RowField {
  Token name;
  TypePtr type = nullptr;
};

struct StructuralRecordType {
  std::vector<RowField> fields;
  std::optional<Token> tail;
};

struct ParenthesizedType {
  TypePtr type = nullptr;
};

struct TypeExpression : Node {
  using Value =
      std::variant<TypeName, UnionType, FunctionType, AppliedType, TupleType,
                   StructuralRecordType, ParenthesizedType>;
  Value value;
};

struct TypeKind {};

struct ArrowKind {
  KindPtr parameter = nullptr;
  KindPtr result = nullptr;
};

struct ParenthesizedKind {
  KindPtr kind = nullptr;
};

struct KindExpression : Node {
  using Value = std::variant<TypeKind, ArrowKind, ParenthesizedKind>;
  Value value;
};

struct GenericParameter {
  Token name;
  std::variant<std::monostate, KindPtr, TypePtr> domain;
};

struct LiteralSpecExpression {
  Token literal;
};

enum class SpecialSpecificationName { Self, Result };

struct NameSpecExpression {
  std::variant<Token, SpecialSpecificationName> name;
};

struct OldSpecExpression {
  SpecExprPtr expression = nullptr;
};

struct ParenthesizedSpecExpression {
  SpecExprPtr expression = nullptr;
};

struct UnarySpecExpression {
  Token op;
  SpecExprPtr operand = nullptr;
};

struct BinarySpecExpression {
  SpecExprPtr left = nullptr;
  Token op;
  SpecExprPtr right = nullptr;
};

struct SpecFieldOperation {
  Token field;
};

struct SpecIndexOperation {
  SpecExprPtr index = nullptr;
};

struct SpecCallOperation {
  std::vector<SpecExprPtr> arguments;
};

using SpecPostfixOperation =
    std::variant<SpecFieldOperation, SpecIndexOperation, SpecCallOperation>;

struct PostfixSpecExpression {
  SpecExprPtr base = nullptr;
  std::vector<SpecPostfixOperation> operations;
};

struct SpecificationExpression : Node {
  using Value =
      std::variant<LiteralSpecExpression, NameSpecExpression, OldSpecExpression,
                   ParenthesizedSpecExpression, UnarySpecExpression,
                   BinarySpecExpression, PostfixSpecExpression>;
  Value value;
};

struct BooleanPredicate {
  Token literal;
};

struct ComparisonPredicate {
  SpecExprPtr left = nullptr;
  Token op;
  SpecExprPtr right = nullptr;
};

struct CallPredicate {
  QualifiedName callee;
  std::vector<SpecExprPtr> arguments;
};

struct OldPredicate {
  SpecExprPtr expression = nullptr;
};

struct ParenthesizedPredicate {
  PredicatePtr predicate = nullptr;
};

struct NegatedPredicate {
  PredicatePtr predicate = nullptr;
};

struct BinaryPredicate {
  PredicatePtr left = nullptr;
  Token op;
  PredicatePtr right = nullptr;
};

enum class Quantifier { Forall, Exists };

struct QuantifierBinding {
  Token name;
  std::variant<TypePtr, KindPtr> domain;
};

struct QuantifiedPredicate {
  Quantifier quantifier = Quantifier::Forall;
  std::vector<QuantifierBinding> bindings;
  PredicatePtr predicate = nullptr;
};

struct Predicate : Node {
  using Value =
      std::variant<BooleanPredicate, ComparisonPredicate, CallPredicate,
                   OldPredicate, ParenthesizedPredicate, NegatedPredicate,
                   BinaryPredicate, QuantifiedPredicate>;
  Value value;
};

struct WildcardPattern {};

struct BindingPattern {
  bool is_mutable = false;
  Token name;
};

struct LiteralPattern {
  Token literal;
};

struct TuplePattern {
  std::vector<PatternPtr> elements;
};

struct VariantPattern {
  QualifiedName name;
  std::optional<std::vector<PatternPtr>> arguments;
};

struct ShorthandRecordPatternField {
  Token name;
};

struct NamedRecordPatternField {
  Token name;
  PatternPtr pattern = nullptr;
};

struct RestRecordPatternField {};

using RecordPatternField =
    std::variant<ShorthandRecordPatternField, NamedRecordPatternField,
                 RestRecordPatternField>;

struct RecordPattern {
  QualifiedName name;
  std::vector<RecordPatternField> fields;
};

struct Pattern : Node {
  using Value = std::variant<WildcardPattern, BindingPattern, LiteralPattern,
                             TuplePattern, VariantPattern, RecordPattern>;
  Value value;
};

struct LiteralExpression {
  Token value;
};

struct NameExpression {
  QualifiedName name;
};

struct ParenthesizedExpression {
  ExprPtr expression = nullptr;
};

struct TupleExpression {
  std::vector<ExprPtr> elements;
};

struct ArrayExpression {
  std::vector<ExprPtr> elements;
};

struct UnaryExpression {
  Token op;
  ExprPtr operand = nullptr;
};

struct BinaryExpression {
  ExprPtr left = nullptr;
  Token op;
  ExprPtr right = nullptr;
};

struct Argument {
  std::optional<Token> name;
  ExprPtr value = nullptr;
};

struct CallOperation {
  std::vector<Argument> arguments;
};

struct GenericApplication {
  std::vector<MetaArgument> arguments;
};

struct FieldOperation {
  Token field;
};

struct IndexOperation {
  ExprPtr index = nullptr;
};

struct PropagationOperation {};

using PostfixOperation =
    std::variant<CallOperation, GenericApplication, FieldOperation,
                 IndexOperation, PropagationOperation>;

struct PostfixExpression {
  ExprPtr base = nullptr;
  std::vector<PostfixOperation> operations;
};

struct IfExpression {
  ExprPtr condition = nullptr;
  BlockPtr then_block = nullptr;
  std::variant<std::monostate, BlockPtr, ExprPtr> else_branch;
};

struct MatchArm {
  PatternPtr pattern = nullptr;
  ExprPtr guard = nullptr;
  std::variant<ExprPtr, BlockPtr> body;
};

struct MatchExpression {
  ExprPtr value = nullptr;
  std::vector<MatchArm> arms;
};

struct RecordInitializer {
  Token name;
  ExprPtr value = nullptr;
};

struct RecordExpression {
  QualifiedName name;
  std::vector<MetaArgument> arguments;
  std::vector<RecordInitializer> fields;
};

struct RecordUpdate {
  Token field;
  ExprPtr value = nullptr;
};

struct RecordUpdateExpression {
  ExprPtr base = nullptr;
  std::vector<RecordUpdate> updates;
};

struct LambdaParameter {
  Token name;
  TypePtr type = nullptr;
};

struct LambdaExpression {
  std::vector<LambdaParameter> parameters;
  TypePtr return_type = nullptr;
  BlockPtr body = nullptr;
};

struct Expression : Node {
  using Value =
      std::variant<LiteralExpression, NameExpression, ParenthesizedExpression,
                   TupleExpression, ArrayExpression, UnaryExpression,
                   BinaryExpression, PostfixExpression, IfExpression,
                   MatchExpression, RecordExpression, RecordUpdateExpression,
                   LambdaExpression>;
  Value value;
};

struct Block : Node {
  std::vector<StmtPtr> statements;
  ExprPtr tail_expression = nullptr;
};

struct LetStatement {
  PatternPtr pattern = nullptr;
  TypePtr type = nullptr;
  ExprPtr value = nullptr;
};

struct VarStatement {
  Token name;
  TypePtr type = nullptr;
  ExprPtr value = nullptr;
};

struct LValueField {
  Token field;
};

struct LValueIndex {
  ExprPtr index = nullptr;
};

using LValuePostfix = std::variant<LValueField, LValueIndex>;

struct LValue {
  Token name;
  std::vector<LValuePostfix> postfixes;
};

struct AssignmentStatement {
  LValue target;
  Token op;
  ExprPtr value = nullptr;
};

struct ReturnStatement {
  ExprPtr value = nullptr;
};

struct BreakStatement {
  ExprPtr value = nullptr;
};

struct ContinueStatement {};

struct WhileStatement {
  ExprPtr condition = nullptr;
  BlockPtr body = nullptr;
};

struct ForStatement {
  PatternPtr pattern = nullptr;
  ExprPtr range = nullptr;
  BlockPtr body = nullptr;
};

struct MutateStatement {
  ExprPtr target = nullptr;
  BlockPtr body = nullptr;
};

struct TransactionStatement {
  ExprPtr target = nullptr;
  BlockPtr body = nullptr;
};

struct ParallelStatement {
  BlockPtr body = nullptr;
};

struct UnsafeStatement {
  BlockPtr body = nullptr;
};

struct ExpressionStatement {
  ExprPtr expression = nullptr;
};

struct Statement : Node {
  using Value = std::variant<LetStatement, VarStatement, AssignmentStatement,
                             ReturnStatement, BreakStatement, ContinueStatement,
                             WhileStatement, ForStatement, MutateStatement,
                             TransactionStatement, ParallelStatement,
                             UnsafeStatement, ExpressionStatement>;
  Value value;
};

struct Parameter {
  Token name;
  bool is_mutable = false;
  TypePtr type = nullptr;
};

struct RequiresClause {
  PredicatePtr predicate = nullptr;
};

struct EnsuresClause {
  PredicatePtr predicate = nullptr;
};

struct UsesClause {
  std::vector<EffectReference> effects;
};

using FunctionClause = std::variant<RequiresClause, EnsuresClause, UsesClause>;

struct FunctionSignature {
  Token name;
  std::vector<GenericParameter> generic_parameters;
  std::vector<Parameter> parameters;
  TypePtr return_type = nullptr;
  std::vector<FunctionClause> clauses;
};

struct FunctionDeclaration : Node {
  bool is_public = false;
  bool is_total = false;
  FunctionSignature signature;
  BlockPtr body = nullptr;
};

struct ExternalFunctionDeclaration : Node {
  bool is_public = false;
  bool is_trusted = false;
  FunctionSignature signature;
};

struct ConstantDeclaration : Node {
  bool is_public = false;
  Token name;
  TypePtr type = nullptr;
  ExprPtr value = nullptr;
};

struct TypeAliasDeclaration : Node {
  bool is_public = false;
  Token name;
  std::vector<GenericParameter> generic_parameters;
  TypePtr type = nullptr;
  PredicatePtr refinement = nullptr;
};

struct FieldDeclaration {
  bool is_public = false;
  Token name;
  TypePtr type = nullptr;
};

struct InvariantDeclaration : Node {
  std::optional<Token> name;
  PredicatePtr predicate = nullptr;
};

using RecordMember = std::variant<FieldDeclaration, InvariantDeclaration *>;

struct RecordTypeDeclaration : Node {
  bool is_public = false;
  Token name;
  std::vector<GenericParameter> generic_parameters;
  std::vector<RecordMember> members;
};

struct VariantField {
  std::optional<Token> name;
  TypePtr type = nullptr;
};

struct VariantDeclaration {
  Token name;
  std::optional<std::vector<VariantField>> fields;
};

struct SumTypeDeclaration : Node {
  bool is_public = false;
  Token name;
  std::vector<GenericParameter> generic_parameters;
  std::vector<VariantDeclaration> variants;
};

struct TraitFunctionDeclaration {
  FunctionSignature signature;
};

struct LawDeclaration : Node {
  Token name;
  PredicatePtr predicate = nullptr;
};

using TraitMember = std::variant<TraitFunctionDeclaration, LawDeclaration *>;

struct TraitDeclaration : Node {
  bool is_public = false;
  Token name;
  std::vector<GenericParameter> generic_parameters;
  std::vector<TraitMember> members;
};

struct TraitReference {
  QualifiedName name;
  std::vector<MetaArgument> arguments;
};

struct ImplDeclaration : Node {
  bool is_public = false;
  TraitReference trait;
  TypePtr target = nullptr;
  std::vector<FunctionDeclaration *> functions;
};

struct CapabilityDeclaration {
  Token name;
};

struct EffectDeclaration : Node {
  bool is_public = false;
  Token name;
  std::optional<std::vector<CapabilityDeclaration>> capabilities;
};

using TypeDeclaration =
    std::variant<TypeAliasDeclaration *, RecordTypeDeclaration *,
                 SumTypeDeclaration *>;

using DomainMember = std::variant<InvariantDeclaration *, FunctionDeclaration *,
                                  TypeDeclaration>;

struct DomainDeclaration : Node {
  bool is_public = false;
  Token name;
  std::vector<DomainMember> members;
};

using TopLevelDeclaration =
    std::variant<FunctionDeclaration *, ExternalFunctionDeclaration *,
                 TypeAliasDeclaration *, RecordTypeDeclaration *,
                 SumTypeDeclaration *, TraitDeclaration *, ImplDeclaration *,
                 ConstantDeclaration *, EffectDeclaration *,
                 DomainDeclaration *>;

struct SourceFile : Node {
  ModuleDeclaration *module = nullptr;
  std::vector<ImportDeclaration *> imports;
  std::vector<TopLevelDeclaration> declarations;
};

using Program = SourceFile;
using Expr = Expression;
using Stmt = Statement;
using LiteralExpr = LiteralExpression;
using UnaryExpr = UnaryExpression;

} // namespace flux::parser
