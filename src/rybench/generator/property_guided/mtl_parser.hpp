#pragma once

#include "mtl_node.hpp"
#include <string_view>
#include <stdexcept>

namespace rybench::generator {

// Parse a formula string.
// Grammar:
//   phi ::= atom | 'not' atom | '(' phi ')' | '(' phi binary_op phi ')' | temporal '[' n ',' n ']' phi
//   binary_op ::= 'and' | 'or' | 'implies' | 'until'
//   temporal  ::= 'eventually' | 'always'
//   atom      ::= identifier
// Throws std::runtime_error on syntax errors.
MTLNode parseMTLNode(std::string_view input);

} // namespace rybench::generator
