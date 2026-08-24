#include <cstdio>
#include <cctype>

static int gettok()
{
    static int lastChar = ' ';

    // skip any whitespace
    while(std::isspace(lastChar))
        lastChar = std::getchar();
}