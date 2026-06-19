#include "mtl_tree.hpp"

namespace rybench::generator {

namespace {

MTLNode cloneNode(const MTLNode& src) {
    MTLNode dst;
    dst.op        = src.op;
    dst.variable  = src.variable;
    dst.negated   = src.negated;
    dst.exclusive = src.exclusive;
    dst.timeLow   = src.timeLow;
    dst.timeHigh  = src.timeHigh;
    if (src.left)  dst.left  = std::make_unique<MTLNode>(cloneNode(*src.left));
    if (src.right) dst.right = std::make_unique<MTLNode>(cloneNode(*src.right));
    if (src.child) dst.child = std::make_unique<MTLNode>(cloneNode(*src.child));
    return dst;
}

} // namespace

MTLNode preprocessFormula(MTLNode node) {
    if (node.left)  *node.left  = preprocessFormula(std::move(*node.left));
    if (node.right) *node.right = preprocessFormula(std::move(*node.right));
    if (node.child) *node.child = preprocessFormula(std::move(*node.child));

    // phi1 implies phi2  →  (not phi1) or (phi1 and phi2)
    if (node.op == MTLNode::Op::Implies) {
        auto phi1 = std::move(node.left);
        auto phi2 = std::move(node.right);

        MTLNode not_phi1;
        not_phi1.op    = MTLNode::Op::Not;
        not_phi1.child = std::make_unique<MTLNode>(cloneNode(*phi1));
        not_phi1 = preprocessFormula(std::move(not_phi1));  // may trigger De Morgan

        MTLNode phi1_and_phi2;
        phi1_and_phi2.op    = MTLNode::Op::And;
        phi1_and_phi2.left  = std::move(phi1);
        phi1_and_phi2.right = std::move(phi2);

        // Branches ¬phi1 and (phi1 ∧ phi2) are mutually exclusive on phi1, so
        // this or must be exclusive — picking both always contradicts on phi1.
        node.op        = MTLNode::Op::Or;
        node.exclusive = true;
        node.left      = std::make_unique<MTLNode>(std::move(not_phi1));
        node.right     = std::make_unique<MTLNode>(std::move(phi1_and_phi2));
        return node;
    }

    // not (phi1 and phi2)  →  (not phi1) or (not phi2)
    if (node.op == MTLNode::Op::Not && node.child &&
        node.child->op == MTLNode::Op::And) {
        auto phi1 = std::move(node.child->left);
        auto phi2 = std::move(node.child->right);
        node.child.reset();

        MTLNode not_phi1;
        not_phi1.op    = MTLNode::Op::Not;
        not_phi1.child = std::move(phi1);
        not_phi1 = preprocessFormula(std::move(not_phi1));

        MTLNode not_phi2;
        not_phi2.op    = MTLNode::Op::Not;
        not_phi2.child = std::move(phi2);
        not_phi2 = preprocessFormula(std::move(not_phi2));

        node.op    = MTLNode::Op::Or;
        node.left  = std::make_unique<MTLNode>(std::move(not_phi1));
        node.right = std::make_unique<MTLNode>(std::move(not_phi2));
        return node;
    }

    // not (phi1 or phi2)  →  (not phi1) and (not phi2)
    if (node.op == MTLNode::Op::Not && node.child &&
        node.child->op == MTLNode::Op::Or) {
        auto phi1 = std::move(node.child->left);
        auto phi2 = std::move(node.child->right);
        node.child.reset();

        MTLNode not_phi1;
        not_phi1.op    = MTLNode::Op::Not;
        not_phi1.child = std::move(phi1);
        not_phi1 = preprocessFormula(std::move(not_phi1));

        MTLNode not_phi2;
        not_phi2.op    = MTLNode::Op::Not;
        not_phi2.child = std::move(phi2);
        not_phi2 = preprocessFormula(std::move(not_phi2));

        node.op    = MTLNode::Op::And;
        node.left  = std::make_unique<MTLNode>(std::move(not_phi1));
        node.right = std::make_unique<MTLNode>(std::move(not_phi2));
        return node;
    }

    // not (eventually[lo,hi] phi)  →  always[lo,hi] (not phi)
    if (node.op == MTLNode::Op::Not && node.child &&
        node.child->op == MTLNode::Op::Eventually) {
        uint64_t lo  = node.child->timeLow;
        uint64_t hi  = node.child->timeHigh;
        auto     phi = std::move(node.child->child);
        node.child.reset();

        MTLNode not_phi;
        not_phi.op    = MTLNode::Op::Not;
        not_phi.child = std::move(phi);
        not_phi = preprocessFormula(std::move(not_phi));

        node.op       = MTLNode::Op::Always;
        node.timeLow  = lo;
        node.timeHigh = hi;
        node.child    = std::make_unique<MTLNode>(std::move(not_phi));
        return node;
    }

    // not (always[lo,hi] phi)  →  eventually[lo,hi] (not phi)
    if (node.op == MTLNode::Op::Not && node.child &&
        node.child->op == MTLNode::Op::Always) {
        uint64_t lo  = node.child->timeLow;
        uint64_t hi  = node.child->timeHigh;
        auto     phi = std::move(node.child->child);
        node.child.reset();

        MTLNode not_phi;
        not_phi.op    = MTLNode::Op::Not;
        not_phi.child = std::move(phi);
        not_phi = preprocessFormula(std::move(not_phi));

        node.op       = MTLNode::Op::Eventually;
        node.timeLow  = lo;
        node.timeHigh = hi;
        node.child    = std::make_unique<MTLNode>(std::move(not_phi));
        return node;
    }

    // not (phi1 until[lo,hi] phi2)   →  (not phi1) release[lo,hi] (not phi2)
    // not (phi1 release[lo,hi] phi2) →  (not phi1) until[lo,hi]   (not phi2)
    // (release is the dual of until; this is what makes negation of `until` reach NNF)
    if (node.op == MTLNode::Op::Not && node.child &&
        (node.child->op == MTLNode::Op::Until ||
         node.child->op == MTLNode::Op::Release)) {
        MTLNode::Op dual = (node.child->op == MTLNode::Op::Until)
            ? MTLNode::Op::Release : MTLNode::Op::Until;
        uint64_t lo   = node.child->timeLow;
        uint64_t hi   = node.child->timeHigh;
        auto     phi1 = std::move(node.child->left);
        auto     phi2 = std::move(node.child->right);
        node.child.reset();

        MTLNode not_phi1;
        not_phi1.op    = MTLNode::Op::Not;
        not_phi1.child = std::move(phi1);
        not_phi1 = preprocessFormula(std::move(not_phi1));

        MTLNode not_phi2;
        not_phi2.op    = MTLNode::Op::Not;
        not_phi2.child = std::move(phi2);
        not_phi2 = preprocessFormula(std::move(not_phi2));

        node.op       = dual;
        node.timeLow  = lo;
        node.timeHigh = hi;
        node.left     = std::make_unique<MTLNode>(std::move(not_phi1));
        node.right    = std::make_unique<MTLNode>(std::move(not_phi2));
        return node;
    }

    return node;
}

MTLNode foldLiterals(MTLNode node) {
    if (node.left)  *node.left  = foldLiterals(std::move(*node.left));
    if (node.right) *node.right = foldLiterals(std::move(*node.right));
    if (node.child) *node.child = foldLiterals(std::move(*node.child));

    // not(atom)  →  signed literal leaf (collapses double negation via XOR on the sign)
    if (node.op == MTLNode::Op::Not && node.child &&
        node.child->op == MTLNode::Op::Atom) {
        MTLNode literal = std::move(*node.child);
        literal.negated = !literal.negated;
        return literal;
    }
    return node;
}

namespace {

std::string boundsStr(const MTLNode& n) {
    std::string hi = (n.timeHigh == std::numeric_limits<uint64_t>::max())
        ? "inf" : std::to_string(n.timeHigh);
    return "[" + std::to_string(n.timeLow) + "," + hi + "]";
}

std::string nodeLabel(const MTLNode& n) {
    switch (n.op) {
        case MTLNode::Op::Atom:       return n.negated ? "¬" + n.variable : n.variable;
        case MTLNode::Op::Not:        return "not";
        case MTLNode::Op::And:        return "and";
        case MTLNode::Op::Or:         return "or";
        case MTLNode::Op::Implies:    return "implies";
        case MTLNode::Op::Eventually: return "eventually" + boundsStr(n);
        case MTLNode::Op::Always:     return "always" + boundsStr(n);
        case MTLNode::Op::Until:      return "until" + boundsStr(n);
        case MTLNode::Op::Release:    return "release" + boundsStr(n);
    }
    return "?";
}

void render(const MTLNode& n, const std::string& prefix, bool isLast,
            bool isRoot, std::string& out) {
    out += prefix;
    if (!isRoot) out += (isLast ? "└── " : "├── ");
    out += nodeLabel(n);
    out += '\n';

    const MTLNode* kids[3];
    int k = 0;
    if (n.left)  kids[k++] = n.left.get();
    if (n.right) kids[k++] = n.right.get();
    if (n.child) kids[k++] = n.child.get();

    std::string childPrefix = prefix;
    if (!isRoot) childPrefix += (isLast ? "    " : "│   ");
    for (int i = 0; i < k; ++i)
        render(*kids[i], childPrefix, i + 1 == k, false, out);
}

} // namespace

std::string toAsciiTree(const MTLNode& node) {
    std::string out;
    render(node, "", true, true, out);
    return out;
}

void collectAtoms(const MTLNode& node, std::unordered_set<std::string>& out) {
    if (node.op == MTLNode::Op::Atom) {
        out.insert(node.variable);
        return;
    }
    if (node.left)  collectAtoms(*node.left,  out);
    if (node.right) collectAtoms(*node.right, out);
    if (node.child) collectAtoms(*node.child, out);
}

} // namespace rybench::generator
