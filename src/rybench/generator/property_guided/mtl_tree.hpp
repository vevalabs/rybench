#pragma once

#include "mtl_node.hpp"
#include <string>
#include <unordered_set>

namespace rybench::generator {

// Rewrite the formula tree by eliminating implies and pushing negations down to atoms (bottom-up):
//   phi1 implies phi2            →  (not phi1) or (phi1 and phi2)
//   not (phi1 and phi2)          →  (not phi1) or  (not phi2)       [De Morgan]
//   not (phi1 or  phi2)          →  (not phi1) and (not phi2)       [De Morgan]
//   not (eventually[lo,hi] phi)  →  always[lo,hi]     (not phi)     [duality]
//   not (always[lo,hi]     phi)  →  eventually[lo,hi] (not phi)     [duality]
// Rules are applied recursively until no more apply.
MTLNode preprocessFormula(MTLNode node);

// Fold leaf negations into signed literals. Requires an NNF tree (run preprocessFormula
// first, so every `not` already sits directly above an atom). Rewrites  not(atom a)  into a
// single Atom leaf with `negated = true`, removing Op::Not from the tree entirely. Double
// negations collapse (not(not a) → a). Non-atom `not` children (shouldn't occur in NNF) are
// left untouched.
MTLNode foldLiterals(MTLNode node);

// Render the formula tree as an indented ASCII tree (box-drawing connectors),
// e.g.
//   or
//   ├── and
//   │   ├── a
//   │   └── c
//   └── eventually[0,1000]
//       └── b
std::string toAsciiTree(const MTLNode& node);

// Collect all atom variable names referenced anywhere in the formula tree.
void collectAtoms(const MTLNode& node, std::unordered_set<std::string>& out);

// Assign a stable depth-first id (root = 0) to every node of the (converted) tree,
// so coverage stats and the ASCII figure refer to the same node numbers.
void assignNodeIds(MTLNode& node);

} // namespace rybench::generator
