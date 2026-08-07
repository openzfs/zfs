/**
 * @name Ambiguous assignment-of-comparison in a condition
 * @description Finds any assignment (`=`, `+=`, `-=`, `|=`, …) that appears in
 *              a branching condition and whose rvalue is an unparenthesized
 *              comparison.
 *
 *              Comparison binds tighter than assignment, so
 *                `if ((x = foo() < 0))`  /  `if ((x += foo() < 0))`
 *              assigns the boolean result of the comparison. Programmers often
 *              meant
 *                `if ((x = foo()) < 0)`  /  `if ((x += foo()) < 0)`
 *              (assign/update then compare). GCC `-Wparentheses` is silenced by
 *              the outer parentheses in both forms, so the compiler does not
 *              catch the mistake (see openzfs/zfs#18874).
 *
 *              Rule (intentionally simple):
 *                assignment in a branch condition
 *                AND rvalue is a comparison
 *                AND that comparison is not parenthesized.
 *
 *              That is enough:
 *                - Explicit assign-then-compare `((x = foo()) < 0)` has no
 *                  comparison as the assignment's rvalue.
 *                - Explicit compare-then-assign `((x = (foo() < 0)))` has a
 *                  parenthesized comparison as the rvalue.
 *                - Parentheses that only wrap a subexpression of the comparison
 *                  (e.g. `(foo())`) do not count as parenthesizing the
 *                  comparison itself.
 * @kind problem
 * @problem.severity error
 * @precision high
 * @id cpp/ambiguous-assignment-of-comparison-in-condition
 * @tags reliability
 *       correctness
 *       external/cwe/cwe-783
 *       external/cwe/cwe-480
 */

import cpp

/**
 * The condition expression of a branching construct
 * (if / while / do-while / for / ternary `?:`).
 */
Expr branchCondition() {
  result = any(IfStmt s).getCondition()
  or
  result = any(WhileStmt s).getCondition()
  or
  result = any(DoStmt s).getCondition()
  or
  result = any(ForStmt s).getCondition()
  or
  result = any(ConditionalExpr c).getCondition()
}

/**
 * Holds if `e` is the branch condition, or appears anywhere under it
 * (including under `&&` / `||` / `!`, and through parentheses/conversions).
 */
predicate inBranchCondition(Expr e) {
  e = branchCondition()
  or
  // Structural child of something already in a branch condition
  exists(Expr parent |
    parent.getAChild() = e and
    inBranchCondition(parent)
  )
  or
  // ParenthesisExpr and other conversions hang off getConversion(), not getAChild()
  exists(Expr base |
    base.getConversion+() = e and
    inBranchCondition(base)
  )
  or
  exists(Expr conv |
    e.getConversion+() = conv and
    inBranchCondition(conv)
  )
}

from Assignment a, ComparisonOperation cmp
where
  // Core rule: assignment in a branch condition whose rvalue is an
  // unparenthesized comparison. Covers `=` and all compound forms (`+=`, …)
  // because Assignment is the common base class.
  a.getRValue() = cmp and
  not cmp.isParenthesised() and
  inBranchCondition(a) and
  // Skip AST from templates that were never instantiated (incomplete / non-code).
  not a.isFromUninstantiatedTemplate(_)
select a,
  "Assignment (`" + a.getOperator() +
    "`) in a branch condition has an unparenthesized comparison as its rvalue; " +
    "likely meant `((x " + a.getOperator() + " expr) op val)` rather than " +
    "`(x " + a.getOperator() + " expr op val)`."
