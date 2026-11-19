# B-Minor syntax highlighter

`bminor-highlight` reads a B-Minor source file. It writes ANSI text for a
terminal or an HTML document for a browser.

## Build and run

Use GNU Make and a C++17 compiler. The test suite also requires GNU
`timeout`.

Run these commands in order.

~~~sh
make
make test
./build/bminor-highlight --format ansi examples/demo.bminor
./build/bminor-highlight --format html examples/demo.bminor > build/demo.html
~~~

Open `build/demo.html` in a browser. The HTML document includes its
stylesheet.

To change the compiler, do these steps.

- Run `make clean`.
- Run `make` with `CXX=clang++` or `CXX=g++`.
- Run `make test` with the same `CXX` value.

| Exit status | Meaning |
| --- | --- |
| 0 | The program reads valid source or prints help for `--help`. |
| 1 | The program writes highlighted source to stdout. It writes lexical or syntax diagnostics to stderr. |
| 2 | The program finds invalid arguments or an input or output error. |

## Test error recovery

Run the example below to check the diagnostics and the HTML output.

~~~sh
cat > build/malformed.bminor <<'BMINOR'
tag: function integer ();
main: function void () = {
    @
    print ;
    print tag();
}
BMINOR
./build/bminor-highlight --format html build/malformed.bminor > build/malformed.html
~~~

The program exits with status `1`. It gives two diagnostics on stderr.

- At line 3, column 5, the scanner reports `LEX_UNKNOWN_BYTE` for `@`. The
  parser skips the invalid token.
- At line 4, column 11, the parser reports `PARSE_EXPECTED`. The `print`
  statement requires an argument before the semicolon.

The parser consumes the semicolon, then continues with the next statement.

The HTML output underlines `@` and the semicolon. It gives the callee
`tag` the function style.

This part of the abstract syntax tree (AST) shows the declaration and the
call. Each `tag` identifier has its own token span.

~~~text
Program
├── FuncDecl
│   ├── Identifier(tag)
│   └── AtomType(integer)
└── FuncDecl
    ├── Identifier(main)
    ├── AtomType(void)
    └── BlockStmt
        └── PrintStmt
            └── CallExpr
                └── Identifier(tag)
~~~

## Analysis stages

- `lex` scans the input bytes once. The tokens cover the input without gaps.
  Each token has its source bytes, byte span and start location. String and
  character tokens also have their decoded bytes.
- `parse` skips trivia and invalid tokens. It uses recursive descent to build
  the AST. After an error, it can continue at a declaration or statement
  boundary. It keeps the completed nodes.
- `classify` gives each token a category. It uses the AST to identify function
  names in declarations and calls. A byte offset connects each name or
  diagnostic to its token.
- `render_ansi` puts Select Graphic Rendition (SGR) codes around the byte
  spans that have a style.
  `render_html` normalizes line breaks and reads UTF-8 across token
  boundaries. It escapes the source text before it writes the HTML.

Phase 1 checks syntax and highlights the source. Each AST node has an empty
type annotation for later phases.

The [lexical rules](docs/lexical.md) give the byte patterns and invalid token
rules. The [grammar](docs/grammar.md) gives the syntax, token choices and
order of AST children.

## Test coverage

| Command | Checks |
| --- | --- |
| `make test-core` | Byte offsets and line breaks, including CRLF |
| `make test-lexer` | Token patterns, literal decoding and lexical diagnostics |
| `make test-parser` | Grammar and AST shape, including precedence and spans |
| `make test-recovery` | Diagnostic locations and AST nodes after errors. Progress on generated inputs. |
| `make test-highlight` | Token categories, AST roles and diagnostic styles |
| `make test-ansi` | Styles and resets. Source bytes after removal of the SGR codes. |
| `make test-cli` | HTML text and styles, including UTF-8. CLI streams and exit statuses. |
