#include <string>
#include <cstdio>
#include <cctype>

enum class Token : int
{
    tok_eof = -1,
    tok_def = -2,
    tok_extern = -3,
    tok_identifier = -4,
    tok_number = -5
};


static double numVal;
static std::string identifierStr;

static int gettok()
{
    static int lastChar = ' ';

    while (std::isspace(lastChar))
        lastChar = std::getchar();

    if (isalpha(lastChar))
    {
        identifierStr = lastChar;
        while (isalnum((lastChar = std::getchar())))
            identifierStr += lastChar;

        if (identifierStr == "def")
            return static_cast<int>(Token::tok_def);
        if (identifierStr == "extern")
            return static_cast<int>(Token::tok_extern);

        return static_cast<int>(Token::tok_identifier);
    }

    if(isdigit(lastChar) || lastChar == '.')
    {
        std::string numStr;
        do
        {
            numStr += lastChar;
            lastChar = getchar();
        }
        while (isdigit(lastChar) || lastChar == '.');

        numVal = strtod(numStr.c_str(), nullptr);
        return static_cast<int>(Token::tok_number);
    }
    
}