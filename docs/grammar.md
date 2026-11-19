# Grammar

| Notation | Meaning |
| --- | --- |
| Quotation marks | Token spelling |
| Square brackets | Optional part |
| Braces | Part that can repeat |

The `atom` rule includes four types.

- `integer`
- `boolean`
- `char`
- `string`

The `literal` rule includes tokens for these four types.
An array bound uses the integer literal token `INTEGER`.

~~~ebnf
program      = { global-decl }, EOF ;
global-decl  = IDENT, ":", ( function-tail | variable-tail ) ;
function-tail = "function", ( atom | "void" ), "(", [ parameters ], ")",
                ( ";" | "=", block ) ;
parameters   = parameter, { ",", parameter } ;
parameter    = IDENT, ":", param-type ;
param-type   = atom | "array", "[", [ INTEGER ], "]", param-type ;
variable-tail = var-type, [ "=", initializer ], ";" ;
var-type     = atom | "array", "[", INTEGER, "]", var-type ;
initializer  = expr | "{", initializer, { ",", initializer }, "}" ;
block        = "{", { statement }, "}" ;
statement    = block
             | IDENT, ":", variable-tail
             | "if", "(", expr, ")", statement, [ "else", statement ]
             | "for", "(", [ expr ], ";", [ expr ], ";", [ expr ], ")", statement
             | "return", [ expr ], ";"
             | "print", arguments, ";"
             | [ expr ], ";" ;
arguments    = expr, { ",", expr } ;
expr         = assignment ;
assignment   = logical-or, [ "=", assignment ] ;
logical-or   = logical-and, { "||", logical-and } ;
logical-and  = comparison, { "&&", comparison } ;
comparison   = additive, { ( "<" | "<=" | ">" | ">=" | "==" | "!=" ), additive } ;
additive     = multiply, { ( "+" | "-" ), multiply } ;
multiply     = power, { ( "*" | "/" | "%" ), power } ;
power        = prefix, [ "^", power ] ;
prefix       = ( "-" | "!" ), prefix | postfix ;
postfix      = primary, { "[", expr, "]" }, [ "++" | "--" ] ;
primary      = IDENT, [ "(", [ arguments ], ")" ] | literal | "(", expr, ")" ;
~~~

## Predictive decisions

Each global declaration starts with `IDENT ":"`. After this prefix, the
parser selects the type of declaration.

- The keyword `function` starts a function tail.
- An atomic type keyword or `array` starts a variable tail.

After the parameters, a `;` ends a function prototype. An `=` starts the
function body. An initializer starts with `{` for a list or with the first
token of an expression.

A primary expression starts with an identifier, a literal or `(`. After
an identifier, `(` starts a call. Subscripts can follow a primary
expression. A single postfix increment or decrement can follow the
subscripts or the primary expression.

A statement can start with `{`, `if`, `for`, `return`, `print` or `;`.
It can also start with an expression. An expression can start with an
identifier, a literal, `(`, `-` or `!`.

If a statement starts with an identifier, the parser checks the next
significant token. A `:` selects a declaration. The parser uses one
significant token for the other choices after a common prefix. It gives
each `else` to the closest unmatched `if`.

A closing `)` ends a parameter list or a call argument list. These lists
can be empty.

A `;` ends the arguments of a `print` statement, and a `}` ends an
initializer list. Each of these lists requires at least one element.

A comma continues any of these lists.

In a `for` header, an immediate `;` omits the first or second expression.
An immediate `)` omits the third expression.

If `;` follows `return` immediately, the return statement has no expression.
A separate `;` is an empty statement.

`EOF` ends the program, which can contain zero declarations.

## Precedence and syntax

The grammar uses precedence levels to prevent left recursion.

Repeated binary operators at one level fold left. Assignment and
exponentiation fold right.

Prefix operators have higher precedence than exponentiation, so `-2^3`
parses as `(-2)^3`. Logical AND has higher precedence than OR. All
comparison operators share one level.

A call uses a bare identifier as its callee. A subscript can follow the
call.

A variable declaration requires an array bound for each dimension. A
parameter can omit a bound with `[]`.

A function is a global declaration. Its return type is an atomic type or
`void`. The language reserves `while`.

The grammar permits unresolved names and assignments such as `1 = 2`.
Later phases check types and assignability.

## AST layout

Each node has a half-open byte span and a 1-based start location. Each name
has its token span. Type annotations start empty.

The table gives the order of the children. A missing optional child in the
table has a null slot.

| Node | Children in order |
| --- | --- |
| Call | Callee, arguments |
| Function | Name, return type, parameters, optional body |
| Variable | Name, type, optional initializer |
| Array type | Optional bound, element type |
| `if` | Condition, then branch, optional else branch |
| `for` | Slots for all three optional expressions, body |
