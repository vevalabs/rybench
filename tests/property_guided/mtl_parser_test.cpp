#include <gtest/gtest.h>
#include "mtl_parser.hpp"
#include "mtl_tree.hpp"

using namespace rybench::generator;

static MTLNode parse(std::string_view s) { return parseMTLNode(s); }

static std::unique_ptr<MTLNode> makeAtom(std::string var) {
    auto n = std::make_unique<MTLNode>();
    n->op       = MTLNode::Op::Atom;
    n->variable = std::move(var);
    return n;
}

// ─── Atoms ────────────────────────────────────────────────────────────────────

TEST(MTLParser, Atom) {
    auto f = parse("a");
    EXPECT_EQ(f.op, MTLNode::Op::Atom);
    EXPECT_EQ(f.variable, "a");
    EXPECT_FALSE(f.left);
    EXPECT_FALSE(f.right);
    EXPECT_FALSE(f.child);
}

TEST(MTLParser, AtomUnderscore) {
    auto f = parse("my_signal");
    EXPECT_EQ(f.op, MTLNode::Op::Atom);
    EXPECT_EQ(f.variable, "my_signal");
}

// ─── Not ──────────────────────────────────────────────────────────────────────

TEST(MTLParser, NotAtom) {
    auto f = parse("not a");
    EXPECT_EQ(f.op, MTLNode::Op::Not);
    ASSERT_TRUE(f.child);
    EXPECT_EQ(f.child->op, MTLNode::Op::Atom);
    EXPECT_EQ(f.child->variable, "a");
}

// ─── Binary operators ─────────────────────────────────────────────────────────

TEST(MTLParser, And) {
    auto f = parse("(a and b)");
    EXPECT_EQ(f.op, MTLNode::Op::And);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);
    EXPECT_EQ(f.left->variable,  "a");
    EXPECT_EQ(f.right->variable, "b");
}

TEST(MTLParser, Or) {
    auto f = parse("(a or b)");
    EXPECT_EQ(f.op, MTLNode::Op::Or);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);
    EXPECT_EQ(f.left->variable,  "a");
    EXPECT_EQ(f.right->variable, "b");
}

TEST(MTLParser, Implies) {
    auto f = parse("(a implies b)");
    EXPECT_EQ(f.op, MTLNode::Op::Implies);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);
    EXPECT_EQ(f.left->variable,  "a");
    EXPECT_EQ(f.right->variable, "b");
}

TEST(MTLParser, Until) {
    auto f = parse("(a until b)");
    EXPECT_EQ(f.op, MTLNode::Op::Until);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);
    EXPECT_EQ(f.left->variable,  "a");
    EXPECT_EQ(f.right->variable, "b");
}

TEST(MTLParser, Release) {
    auto f = parse("(a release b)");
    EXPECT_EQ(f.op, MTLNode::Op::Release);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);
    EXPECT_EQ(f.left->variable,  "a");
    EXPECT_EQ(f.right->variable, "b");
}

TEST(MTLParser, ReleaseBounded) {
    auto f = parse("(a release[0,100] b)");
    EXPECT_EQ(f.op, MTLNode::Op::Release);
    EXPECT_EQ(f.timeLow,  uint64_t{0});
    EXPECT_EQ(f.timeHigh, uint64_t{100});
}

// ─── Temporal operators ───────────────────────────────────────────────────────

TEST(MTLParser, Eventually) {
    auto f = parse("eventually[0,100] a");
    EXPECT_EQ(f.op, MTLNode::Op::Eventually);
    EXPECT_EQ(f.timeLow,  uint64_t{0});
    EXPECT_EQ(f.timeHigh, uint64_t{100});
    ASSERT_TRUE(f.child);
    EXPECT_EQ(f.child->variable, "a");
}

TEST(MTLParser, Always) {
    auto f = parse("always[5,50] b");
    EXPECT_EQ(f.op, MTLNode::Op::Always);
    EXPECT_EQ(f.timeLow,  uint64_t{5});
    EXPECT_EQ(f.timeHigh, uint64_t{50});
    ASSERT_TRUE(f.child);
    EXPECT_EQ(f.child->variable, "b");
}

TEST(MTLParser, EventuallyZeroBounds) {
    auto f = parse("eventually[0,0] a");
    EXPECT_EQ(f.timeLow,  uint64_t{0});
    EXPECT_EQ(f.timeHigh, uint64_t{0});
}

// ─── Nested formulas ──────────────────────────────────────────────────────────

TEST(MTLParser, NestedBinary) {
    // (a and (b or c))
    auto f = parse("(a and (b or c))");
    EXPECT_EQ(f.op, MTLNode::Op::And);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);
    EXPECT_EQ(f.left->variable, "a");
    EXPECT_EQ(f.right->op, MTLNode::Op::Or);
    ASSERT_TRUE(f.right->left);
    ASSERT_TRUE(f.right->right);
    EXPECT_EQ(f.right->left->variable,  "b");
    EXPECT_EQ(f.right->right->variable, "c");
}

TEST(MTLParser, EventuallyOfNot) {
    auto f = parse("eventually[0,10] not a");
    EXPECT_EQ(f.op, MTLNode::Op::Eventually);
    ASSERT_TRUE(f.child);
    EXPECT_EQ(f.child->op, MTLNode::Op::Not);
    ASSERT_TRUE(f.child->child);
    EXPECT_EQ(f.child->child->variable, "a");
}

TEST(MTLParser, AlwaysOfBinary) {
    auto f = parse("always[0,100] (a and b)");
    EXPECT_EQ(f.op, MTLNode::Op::Always);
    ASSERT_TRUE(f.child);
    EXPECT_EQ(f.child->op, MTLNode::Op::And);
}

TEST(MTLParser, BinaryOfTemporals) {
    auto f = parse("(eventually[0,5] a or always[0,5] b)");
    EXPECT_EQ(f.op, MTLNode::Op::Or);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);
    EXPECT_EQ(f.left->op,  MTLNode::Op::Eventually);
    EXPECT_EQ(f.right->op, MTLNode::Op::Always);
}

// ─── Schema example ───────────────────────────────────────────────────────────

TEST(MTLParser, SchemaExampleFormula) {
    // From mtl-example.fbs: ((a and c) or (eventually[0,1000] b))
    auto f = parse("((a and c) or (eventually[0,1000] b))");

    EXPECT_EQ(f.op, MTLNode::Op::Or);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);

    EXPECT_EQ(f.left->op, MTLNode::Op::And);
    EXPECT_EQ(f.left->left->variable,  "a");
    EXPECT_EQ(f.left->right->variable, "c");

    EXPECT_EQ(f.right->op, MTLNode::Op::Eventually);
    EXPECT_EQ(f.right->timeLow,  uint64_t{0});
    EXPECT_EQ(f.right->timeHigh, uint64_t{1000});
    ASSERT_TRUE(f.right->child);
    EXPECT_EQ(f.right->child->variable, "b");
}

// ─── Whitespace handling ──────────────────────────────────────────────────────

TEST(MTLParser, ExtraWhitespace) {
    auto f = parse("  (  a   and   b  )  ");
    EXPECT_EQ(f.op, MTLNode::Op::And);
    EXPECT_EQ(f.left->variable,  "a");
    EXPECT_EQ(f.right->variable, "b");
}

// ─── Complex formulas ─────────────────────────────────────────────────────────

TEST(MTLParser, CompoundImpliesAndAlways) {
    //
    //                          And
    //                        /     \
    //                  Implies         Or
    //                  /     \     /        \
    //          Ev[0,100]    Not  Al[0,10]  Al[0,2]
    //              |          |      |         |
    //              Or         c      a         Or
    //             / \                        /    \
    //            a   b               Ev[0,7]      Not
    //                                    |          |
    //                                    b          c
    //

    
    auto f = parse(
        "(((eventually[0,100] (a or b)) implies not c)"
        " and"
        " ((always[0,10] a) or (always[0,2] ((eventually[0,7] b) or not c))))");

    ASSERT_EQ(f.op, MTLNode::Op::And);
    ASSERT_TRUE(f.left);
    ASSERT_TRUE(f.right);

    // left: (eventually[0,100] (a or b)) implies (not c)
    const MTLNode& impl = *f.left;
    ASSERT_EQ(impl.op, MTLNode::Op::Implies);
    ASSERT_TRUE(impl.left);
    ASSERT_TRUE(impl.right);

    // impl.left: eventually[0,100] (a or b)
    ASSERT_EQ(impl.left->op,       MTLNode::Op::Eventually);
    ASSERT_EQ(impl.left->timeLow,  uint64_t{0});
    ASSERT_EQ(impl.left->timeHigh, uint64_t{100});
    ASSERT_TRUE(impl.left->child);
    ASSERT_EQ(impl.left->child->op, MTLNode::Op::Or);
    ASSERT_EQ(impl.left->child->left->variable,  "a");
    ASSERT_EQ(impl.left->child->right->variable, "b");

    // impl.right: not c
    ASSERT_EQ(impl.right->op, MTLNode::Op::Not);
    ASSERT_TRUE(impl.right->child);
    ASSERT_EQ(impl.right->child->variable, "c");

    // right: (always[0,10] a) or (always[0,2] (...))
    const MTLNode& disj = *f.right;
    ASSERT_EQ(disj.op, MTLNode::Op::Or);
    ASSERT_TRUE(disj.left);
    ASSERT_TRUE(disj.right);

    // disj.left: always[0,10] a
    ASSERT_EQ(disj.left->op,       MTLNode::Op::Always);
    ASSERT_EQ(disj.left->timeLow,  uint64_t{0});
    ASSERT_EQ(disj.left->timeHigh, uint64_t{10});
    ASSERT_TRUE(disj.left->child);
    ASSERT_EQ(disj.left->child->variable, "a");

    // disj.right: always[0,2] ((eventually[0,7] b) or (not c))
    ASSERT_EQ(disj.right->op,       MTLNode::Op::Always);
    ASSERT_EQ(disj.right->timeLow,  uint64_t{0});
    ASSERT_EQ(disj.right->timeHigh, uint64_t{2});
    ASSERT_TRUE(disj.right->child);

    const MTLNode& inner = *disj.right->child;
    ASSERT_EQ(inner.op, MTLNode::Op::Or);
    ASSERT_TRUE(inner.left);
    ASSERT_TRUE(inner.right);

    // inner.left: eventually[0,7] b
    ASSERT_EQ(inner.left->op,       MTLNode::Op::Eventually);
    ASSERT_EQ(inner.left->timeLow,  uint64_t{0});
    ASSERT_EQ(inner.left->timeHigh, uint64_t{7});
    ASSERT_TRUE(inner.left->child);
    ASSERT_EQ(inner.left->child->variable, "b");

    // inner.right: not c
    ASSERT_EQ(inner.right->op, MTLNode::Op::Not);
    ASSERT_TRUE(inner.right->child);
    ASSERT_EQ(inner.right->child->variable, "c");
}


TEST(MTLPreprocess, CompoundFormula) {
    //  Input (raw parse tree):
    //
    //                          And
    //                        /     \
    //                  Implies         Or
    //                  /     \     /        \
    //          Ev[0,100]     Not Al[0,10]  Al[0,2]
    //              |          |      |         |
    //              Or         c      a         Or
    //             / \                        /    \
    //            a   b                Ev[0,7]      Not
    //                                    |          |
    //                                    b          c


    //  After preprocessFormula :
    //
    //                                 And                                                                                                
    //                            /          \         
    //                   Or                            Or                                                                                            
    //                /      \                     /        \                                                                                     
    //           Al[0,100]     And               Al[0,10]  Al[0,2]                                                                              
    //              |         /   \                |         |                                                                                
    //             And   Ev[0,100] Not             a         Or                                                                            
    //             / \        |     |                      /    \                                                                            
    //            a   b       Or    c                  Ev[0,7]   Not                                                                          
    //                       / \                           |       |                                                                          
    //                      a   b                          b       c                                                                          
    //                                                           
   
    
    auto f = preprocessFormula(parse(
        "(((eventually[0,100] (a or b)) implies not c)"
        " and"
        " ((always[0,10] a) or (always[0,2] ((eventually[0,7] b) or not c))))"));

    // root: And
    ASSERT_EQ(f.op, MTLNode::Op::And);

    // left subtree: Or(Al[0,100](And(Not(a),Not(b))), And(Ev[0,100](Or(a,b)), Not(c)))
    const MTLNode& lhs = *f.left;
    ASSERT_EQ(lhs.op, MTLNode::Op::Or);

    // lhs.left: Al[0,100](And(Not(a), Not(b)))
    ASSERT_EQ(lhs.left->op,                        MTLNode::Op::Always);
    ASSERT_EQ(lhs.left->timeLow,                   uint64_t{0});
    ASSERT_EQ(lhs.left->timeHigh,                  uint64_t{100});
    ASSERT_EQ(lhs.left->child->op,                 MTLNode::Op::And);
    ASSERT_EQ(lhs.left->child->left->op,           MTLNode::Op::Not);
    ASSERT_EQ(lhs.left->child->left->child->variable,  "a");
    ASSERT_EQ(lhs.left->child->right->op,          MTLNode::Op::Not);
    ASSERT_EQ(lhs.left->child->right->child->variable, "b");

    // lhs.right: And(Ev[0,100](Or(a,b)), Not(c))
    ASSERT_EQ(lhs.right->op,                    MTLNode::Op::And);
    ASSERT_EQ(lhs.right->left->op,              MTLNode::Op::Eventually);
    ASSERT_EQ(lhs.right->left->timeLow,         uint64_t{0});
    ASSERT_EQ(lhs.right->left->timeHigh,        uint64_t{100});
    ASSERT_EQ(lhs.right->left->child->op,       MTLNode::Op::Or);
    ASSERT_EQ(lhs.right->left->child->left->variable,  "a");
    ASSERT_EQ(lhs.right->left->child->right->variable, "b");
    ASSERT_EQ(lhs.right->right->op,             MTLNode::Op::Not);
    ASSERT_EQ(lhs.right->right->child->variable, "c");

    // right subtree: Or(Al[0,10](a), Al[0,2](Or(Ev[0,7](b), Not(c))))  — unchanged
    const MTLNode& rhs = *f.right;
    ASSERT_EQ(rhs.op, MTLNode::Op::Or);

    ASSERT_EQ(rhs.left->op,       MTLNode::Op::Always);
    ASSERT_EQ(rhs.left->timeLow,  uint64_t{0});
    ASSERT_EQ(rhs.left->timeHigh, uint64_t{10});
    ASSERT_EQ(rhs.left->child->variable, "a");

    ASSERT_EQ(rhs.right->op,       MTLNode::Op::Always);
    ASSERT_EQ(rhs.right->timeLow,  uint64_t{0});
    ASSERT_EQ(rhs.right->timeHigh, uint64_t{2});
    const MTLNode& inner = *rhs.right->child;
    ASSERT_EQ(inner.op, MTLNode::Op::Or);
    ASSERT_EQ(inner.left->op,           MTLNode::Op::Eventually);
    ASSERT_EQ(inner.left->timeLow,      uint64_t{0});
    ASSERT_EQ(inner.left->timeHigh,     uint64_t{7});
    ASSERT_EQ(inner.left->child->variable, "b");
    ASSERT_EQ(inner.right->op,          MTLNode::Op::Not);
    ASSERT_EQ(inner.right->child->variable, "c");
}



// ─── Literal folding ─────────────────────────────────────────────────────────

static bool hasNotOp(const MTLNode& n) {
    if (n.op == MTLNode::Op::Not) return true;
    if (n.left  && hasNotOp(*n.left))  return true;
    if (n.right && hasNotOp(*n.right)) return true;
    if (n.child && hasNotOp(*n.child)) return true;
    return false;
}

TEST(MTLLiterals, FoldNotAtom) {
    // not a  →  single Atom leaf with negated = true
    auto f = foldLiterals(parse("not a"));
    EXPECT_EQ(f.op, MTLNode::Op::Atom);
    EXPECT_TRUE(f.negated);
    EXPECT_EQ(f.variable, "a");
    EXPECT_FALSE(f.child);
}

TEST(MTLLiterals, PlainAtomUnchanged) {
    auto f = foldLiterals(parse("a"));
    EXPECT_EQ(f.op, MTLNode::Op::Atom);
    EXPECT_FALSE(f.negated);
    EXPECT_EQ(f.variable, "a");
}

TEST(MTLLiterals, DoubleNegationCollapses) {
    // not(not a) — build directly, then fold → plain a
    MTLNode inner;
    inner.op    = MTLNode::Op::Not;
    inner.child = makeAtom("a");
    MTLNode outer;
    outer.op    = MTLNode::Op::Not;
    outer.child = std::make_unique<MTLNode>(std::move(inner));

    auto f = foldLiterals(std::move(outer));
    EXPECT_EQ(f.op, MTLNode::Op::Atom);
    EXPECT_FALSE(f.negated);
    EXPECT_EQ(f.variable, "a");
}

TEST(MTLLiterals, NoNotOpsAfterFold) {
    // (a implies b) → (not a) or (a and b);  fold → (¬a) or (a and b), zero Not ops
    auto f = foldLiterals(preprocessFormula(parse("(a implies b)")));
    EXPECT_FALSE(hasNotOp(f));

    ASSERT_EQ(f.op, MTLNode::Op::Or);
    ASSERT_EQ(f.left->op, MTLNode::Op::Atom);
    EXPECT_TRUE(f.left->negated);
    EXPECT_EQ(f.left->variable, "a");
    ASSERT_EQ(f.right->op, MTLNode::Op::And);
    EXPECT_FALSE(f.right->left->negated);
    EXPECT_FALSE(f.right->right->negated);
}

TEST(MTLLiterals, DeMorganLeavesBecomeLiterals) {
    // not(a and b) → (not a) or (not b);  fold → (¬a) or (¬b)
    auto f = foldLiterals(preprocessFormula(parse("(a implies (a and b))")));
    EXPECT_FALSE(hasNotOp(f));
}

// ─── Error cases ──────────────────────────────────────────────────────────────

TEST(MTLParser, EmptyInputThrows) {
    EXPECT_THROW(parse(""), std::runtime_error);
}

TEST(MTLParser, TrailingTokenThrows) {
    EXPECT_THROW(parse("a b"), std::runtime_error);
}

TEST(MTLParser, MissingClosingParenThrows) {
    EXPECT_THROW(parse("(a and b"), std::runtime_error);
}

TEST(MTLParser, UnknownBinaryOpThrows) {
    EXPECT_THROW(parse("(a xor b)"), std::runtime_error);
}

TEST(MTLParser, MissingBoundsThrows) {
    EXPECT_THROW(parse("eventually a"), std::runtime_error);
}

TEST(MTLParser, MissingCommaInBoundsThrows) {
    EXPECT_THROW(parse("eventually[0 100] a"), std::runtime_error);
}

TEST(MTLParser, NotOnCompoundParses) {
    // 'not' applies to any subformula now (the preprocessor pushes it to NNF).
    auto f = parse("not (a and b)");
    EXPECT_EQ(f.op, MTLNode::Op::Not);
    ASSERT_TRUE(f.child);
    EXPECT_EQ(f.child->op, MTLNode::Op::And);
    EXPECT_EQ(f.child->left->variable,  "a");
    EXPECT_EQ(f.child->right->variable, "b");
}

TEST(MTLParser, NotOnTemporalParses) {
    // not eventually[...] r  → Not over an Eventually node
    auto f = parse("not eventually[0,5] r");
    EXPECT_EQ(f.op, MTLNode::Op::Not);
    ASSERT_TRUE(f.child);
    EXPECT_EQ(f.child->op, MTLNode::Op::Eventually);
    EXPECT_EQ(f.child->child->variable, "r");
}

// ─── Preprocess: implies ─────────────────────────────────────────────────────

TEST(MTLPreprocess, ImpliesAtoms) {
    //  (a implies b)  →  (not a) or (a and b)
    //
    //      Or
    //     /  \
    //   Not  And
    //    |   / \
    //    a  a   b
    //
    auto f = preprocessFormula(parse("(a implies b)"));

    ASSERT_EQ(f.op, MTLNode::Op::Or);

    ASSERT_EQ(f.left->op, MTLNode::Op::Not);
    ASSERT_EQ(f.left->child->variable, "a");

    ASSERT_EQ(f.right->op, MTLNode::Op::And);
    ASSERT_EQ(f.right->left->variable,  "a");
    ASSERT_EQ(f.right->right->variable, "b");
}

TEST(MTLPreprocess, ImpliesWithTemporals) {
    //  (eventually[0,5] a  implies  always[0,3] b)
    //  →  (always[0,5] (not a)) or (eventually[0,5] a and always[0,3] b)
    //
    //  not(Ev[0,5](a)) fires duality → Al[0,5](Not(a))
    auto f = preprocessFormula(parse("(eventually[0,5] a implies always[0,3] b)"));

    ASSERT_EQ(f.op, MTLNode::Op::Or);

    // left: always[0,5] (not a)   ← duality on not(eventually)
    ASSERT_EQ(f.left->op,             MTLNode::Op::Always);
    ASSERT_EQ(f.left->timeLow,        uint64_t{0});
    ASSERT_EQ(f.left->timeHigh,       uint64_t{5});
    ASSERT_EQ(f.left->child->op,      MTLNode::Op::Not);
    ASSERT_EQ(f.left->child->child->variable, "a");

    // right: eventually[0,5] a  and  always[0,3] b
    ASSERT_EQ(f.right->op,              MTLNode::Op::And);
    ASSERT_EQ(f.right->left->op,        MTLNode::Op::Eventually);
    ASSERT_EQ(f.right->left->timeLow,   uint64_t{0});
    ASSERT_EQ(f.right->left->timeHigh,  uint64_t{5});
    ASSERT_EQ(f.right->right->op,       MTLNode::Op::Always);
    ASSERT_EQ(f.right->right->timeLow,  uint64_t{0});
    ASSERT_EQ(f.right->right->timeHigh, uint64_t{3});
}

// ─── Preprocess: until / release duality ─────────────────────────────────────

TEST(MTLPreprocess, NotUntilBecomesRelease) {
    //  not (a until[0,100] b)  →  (not a) release[0,100] (not b)
    auto f = preprocessFormula(parse("not (a until[0,100] b)"));

    ASSERT_EQ(f.op, MTLNode::Op::Release);
    EXPECT_EQ(f.timeLow,  uint64_t{0});
    EXPECT_EQ(f.timeHigh, uint64_t{100});
    ASSERT_EQ(f.left->op,  MTLNode::Op::Not);
    EXPECT_EQ(f.left->child->variable,  "a");
    ASSERT_EQ(f.right->op, MTLNode::Op::Not);
    EXPECT_EQ(f.right->child->variable, "b");
}

TEST(MTLPreprocess, NotReleaseBecomesUntil) {
    //  not (a release b)  →  (not a) until (not b)
    auto f = preprocessFormula(parse("not (a release b)"));

    ASSERT_EQ(f.op, MTLNode::Op::Until);
    ASSERT_EQ(f.left->op,  MTLNode::Op::Not);
    EXPECT_EQ(f.left->child->variable,  "a");
    ASSERT_EQ(f.right->op, MTLNode::Op::Not);
    EXPECT_EQ(f.right->child->variable, "b");
}

TEST(MTLLiterals, NotUntilFoldsToReleaseOfLiterals) {
    //  not (a until b)  →  (¬a) release (¬b)  with literals folded, no Not ops
    auto f = foldLiterals(preprocessFormula(parse("not (a until b)")));
    EXPECT_FALSE(hasNotOp(f));
    ASSERT_EQ(f.op, MTLNode::Op::Release);
    EXPECT_EQ(f.left->op,  MTLNode::Op::Atom);
    EXPECT_TRUE(f.left->negated);
    EXPECT_EQ(f.right->op, MTLNode::Op::Atom);
    EXPECT_TRUE(f.right->negated);
}

// ─── Preprocess: De Morgan ───────────────────────────────────────────────────

TEST(MTLPreprocess, NotAndDemorgan) {
    //  not (a and b)  →  (not a) or (not b)
    MTLNode in;
    in.op    = MTLNode::Op::Not;
    in.child = std::make_unique<MTLNode>();
    in.child->op    = MTLNode::Op::And;
    in.child->left  = makeAtom("a");
    in.child->right = makeAtom("b");

    auto f = preprocessFormula(std::move(in));

    ASSERT_EQ(f.op, MTLNode::Op::Or);
    ASSERT_EQ(f.left->op,            MTLNode::Op::Not);
    ASSERT_EQ(f.left->child->variable,  "a");
    ASSERT_EQ(f.right->op,           MTLNode::Op::Not);
    ASSERT_EQ(f.right->child->variable, "b");
}

TEST(MTLPreprocess, NotOrDemorgan) {
    //  not (a or b)  →  (not a) and (not b)
    MTLNode in;
    in.op    = MTLNode::Op::Not;
    in.child = std::make_unique<MTLNode>();
    in.child->op    = MTLNode::Op::Or;
    in.child->left  = makeAtom("a");
    in.child->right = makeAtom("b");

    auto f = preprocessFormula(std::move(in));

    ASSERT_EQ(f.op, MTLNode::Op::And);
    ASSERT_EQ(f.left->op,            MTLNode::Op::Not);
    ASSERT_EQ(f.left->child->variable,  "a");
    ASSERT_EQ(f.right->op,           MTLNode::Op::Not);
    ASSERT_EQ(f.right->child->variable, "b");
}

TEST(MTLPreprocess, NestedDeMorganPropagates) {
    //  not (not (a and b))  →  not ((not a) or (not b))
    //  (double-not wrapping an and — inner De Morgan fires, outer Not stays)
    MTLNode and_node;
    and_node.op    = MTLNode::Op::And;
    and_node.left  = makeAtom("a");
    and_node.right = makeAtom("b");

    MTLNode inner_not;
    inner_not.op    = MTLNode::Op::Not;
    inner_not.child = std::make_unique<MTLNode>(std::move(and_node));

    MTLNode outer_not;
    outer_not.op    = MTLNode::Op::Not;
    outer_not.child = std::make_unique<MTLNode>(std::move(inner_not));

    // After bottom-up preprocessFormula:
    //   inner not(and) → or(not a, not b)    [De Morgan on inner]
    //   outer not wraps an Or  → and(not(not a), not(not b))  [De Morgan on outer]
    auto f = preprocessFormula(std::move(outer_not));

    ASSERT_EQ(f.op, MTLNode::Op::And);
    ASSERT_EQ(f.left->op,  MTLNode::Op::Not);
    ASSERT_EQ(f.right->op, MTLNode::Op::Not);
    // inner not-not-a
    ASSERT_EQ(f.left->child->op,           MTLNode::Op::Not);
    ASSERT_EQ(f.left->child->child->variable,  "a");
    ASSERT_EQ(f.right->child->op,          MTLNode::Op::Not);
    ASSERT_EQ(f.right->child->child->variable, "b");
}
