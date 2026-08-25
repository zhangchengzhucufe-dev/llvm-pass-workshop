#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

enum class Token : int {
  tok_eof = -1,
  tok_def = -2,
  tok_extern = -3,
  tok_identifier = -4,
  tok_number = -5
};

static double NumVal;
static std::string IdentifierStr;

static int gettok() {
  static int LastChar = ' ';

  while (std::isspace(static_cast<unsigned char>(LastChar)))
    LastChar = std::getchar();

  if (isalpha(LastChar)) {
    IdentifierStr.clear();
    IdentifierStr = static_cast<char>(LastChar);
    while (isalnum(static_cast<unsigned char>(((LastChar = std::getchar())))))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def") return static_cast<int>(Token::tok_def);
    if (IdentifierStr == "extern") return static_cast<int>(Token::tok_extern);

    return static_cast<int>(Token::tok_identifier);
  }

  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += static_cast<char>(LastChar);
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');
    NumVal = strtod(NumStr.c_str(), nullptr);
    return static_cast<int>(Token::tok_number);
  }

  if (LastChar == '#') {
    do LastChar = std::getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF) return gettok();
  }

  if (LastChar == EOF) return static_cast<int>(Token::tok_eof);

  int thisChar = LastChar;
  LastChar = std::getchar();
  return thisChar;
}
