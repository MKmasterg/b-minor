# Lexical rules

The patterns below match bytes. A named subexpression identifies a group.

| Symbol | Meaning |
| --- | --- |
| `\xNN` | Hexadecimal byte value |
| `\r` | Carriage return (CR) |
| `\n` | Line feed (LF) |
| `\t` | Tab |

~~~text
IDENTIFIER    [A-Za-z_][A-Za-z0-9_]{0,255}
KEYWORD       array|boolean|char|else|for|function|if|integer|print|return|string|void|while
BOOLEAN       true|false
INTEGER       [0-9]+
ESCAPE        \\[\x00-\x09\x0B-\x0C\x0E-\x7F]
STRING_BYTE   [\x00-\x09\x0B-\x0C\x0E-\x21\x23-\x5B\x5D-\x7F]
CHAR_BYTE     [\x00-\x09\x0B-\x0C\x0E-\x26\x28-\x5B\x5D-\x7F]
STRING        "(STRING_BYTE|ESCAPE){0,256}"
CHARACTER     '(CHAR_BYTE|ESCAPE)'
OPERATOR      \+\+|--|<=|>=|==|!=|&&|\|\||[+\-*/%^!<>=]
DELIMITER     [()\[\]{},:;]
LINE_COMMENT  //[^\r\n]*
BLOCK_COMMENT /\*([^*]|\*+[^*/])*\*+/
WHITESPACE    [ \t\r\n]+
~~~

## Token selection

At each byte offset, the scanner takes the longest token. It reads the full
identifier before it compares the spelling with the keywords and Boolean
literals. The comparison is case-sensitive.

The scanner treats `return_` as an identifier and `false` as a Boolean
literal. The word `while` is a reserved keyword.

Integers keep their leading zeros. The scanner gives a minus sign its
own token.

A comment opener has priority over `/`. Comments can contain bytes outside
the ASCII range.

The first `*/` ends a block comment. A line comment ends before CR or LF,
or at the end of the file (EOF).

The scanner checks two-byte operators before their one-byte prefixes. It
puts consecutive whitespace bytes in one token.

## Quoted literals

After a backslash, the scanner takes the next byte if it exists and differs
from CR and LF. The escapes `\n` and `\0` give LF and NUL, in that order.
Other escapes give their second byte. The scanner treats an escaped quote as
a payload byte.

The payload and escape bytes must be ASCII.

A string can have a maximum of
256 decoded bytes. A character literal must have exactly one decoded byte.

## Invalid input

The scanner gives one diagnostic for the full span of each invalid token.
It continues at the next byte after the span.

- If an identifier has more than 256 bytes, the scanner reads the full
  identifier and gives `LEX_IDENT_LENGTH`.
- For a closed literal with invalid payload bytes or an invalid length, the
  scanner gives `LEX_BAD_LITERAL`.
- If a quoted literal has no closing quote, the scanner stops before CR or
  LF, or at EOF. It gives `LEX_UNTERMINATED_STRING` for a string or
  `LEX_UNTERMINATED_CHAR` for a character literal. A missing quote takes
  priority over a payload or length error.
- If a block comment has no `*/`, the scanner continues to EOF. It gives
  `LEX_UNTERMINATED_COMMENT`.
- If a byte cannot start a token, the scanner takes only that byte. It gives
  `LEX_UNKNOWN_BYTE`.

## Token positions

Each token keeps its source bytes. The scanner adds one empty EOF token at
the final byte offset.

Line and column numbers start at 1. A CRLF pair starts one new line. A CR or
LF also starts a new line when it occurs alone. All other bytes increase
the column by one.
