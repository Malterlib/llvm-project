# Malterlib Formatting Rules

This document distills the Malterlib coding-style specification from `MalterlibFormatDocs.md` into a concise, implementation-oriented ruleset.  Each item is expressed in a form suitable for direct translation into clang-format options or dedicated formatting logic inside **MalterlibFormatter**.

---

## 1. Global

* **Indentation**
  * One *tab* character per logical indent level (tabs **never** expanded to spaces).
  * A tab corresponds to **4 columns**.

* **Column limit**
  * Hard limit: **190 columns**.

* **Trailing whitespace**
  * Prohibited on every line.

---

## 2. Whitespace Around Tokens

* **Binary / unary operators**
  * Exactly one space *before* and *after* all operators **except**:
    * Member-access operators `.` and `->` (no surrounding spaces).
    * Unary dereference `*` (no space between `*` and the operand).
  * Comma operator: space **after**, but **not** before the comma.

* **Parentheses**
  * A space between a control-flow keyword and the opening parenthesis: `if (cond)`.
  * No spaces just inside `(` or just before `)`.

* **Braces**
  * Opening brace `{` starts on its **own line**, vertically aligned with the keyword it belongs to.
  * Closing brace `}` aligned with its corresponding opening keyword.
  * Single-statement **if / for / while / do** bodies omit braces.

---

## 3. Statement Breaking

* When a statement spans multiple lines, **each syntactic unit at the same nesting level appears on its own line**.
* All scope markers (`{`, `}`) and member-access chains (`.`, `->`) must reside on lines by themselves once a break occurs.
* Example rule implementation hints:
  * Break *before* every argument after the first when a call is broken.
  * Break *after* each binary operator when the left-hand and right-hand operands live on separate lines.

---

## 4. Control-Flow Constructs

### if / else

* `else` is placed directly under its matching `if` (no `else if` on same line).
* Braces follow the global brace rules.

### for / while / do-while

* The three `for` clauses may be split vertically, one per line, when the loop head is broken.
* The `while` condition in a `do-while` stays on the same line as `while` keyword when it fits; otherwise same multi-line style as other control keywords.

### switch / case

* `case` / `default` labels are aligned with `switch`.
* A single `statement; break;` sequence *may* live on the same line as the label.
* It is **not** permitted to place multiple statements on the same line.

---

## 5. Function Declarations / Definitions

Ordering obligatory modifiers:
`[static] [virtual] [inline] [other_attributes] <return-type> f_Name(<params>) [const] [volatile] [= 0];`

* **Do not** split before the function name.  Use a *trailing return type* (`auto … -> return_type`) to save space instead.
* If a declaration/definition breaks **after** the name line, every subsequent line is indented one level (tab).
* Parameter lists may be split such that **each parameter is on its own line**, comma leading.

---

## 6. Class / Struct / Union Layout

Section order inside aggregates:
`public` → `protected` → `private`.

Inside each section the ordering is:
1. Types (using / typedef / nested structs)
2. Constructors
3. Destructor
4. Assignment operators
5. Comparison operators
6. Other operators
7. Functions
8. `constexpr static` member variables
9. Static member variables
10. Non-static member variables

---

## 7. Naming & Miscellaneous Conventions (inferred from examples)

* Public member variables prefixed with `m_`.
* Protected member variables prefixed with `mp_`.
* Functions use `f_` prefix for free functions and `fp_` for protected/private methods.
* Template parameter aliases begin with `T` or `TC` (e.g., `TCTemplate`).
* Return-value aliases often suffixed with `RetValue` / `CType`.

While not enforced by the formatter itself, these conventions are noted for completeness.

---

## 8. Concepts & Requires Clauses

* `requires` clauses always appear **on their own line**, indented one level beneath the entity they modify.

---

## 9. Future Work

This rule list is intended to guide the implementation of `MalterlibFormatter::analyze()`.  Ambiguities or omissions discovered during implementation should result in additions to this document so that the formatter behavior remains transparent and reproducible. 