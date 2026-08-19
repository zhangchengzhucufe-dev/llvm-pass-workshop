#include <cstdio>
#include <cctype>

static int gettok()
{
    static int lastChar = ' ';
    while(std::isspace(lastChar))
    {
        lastChar = getchar();
    }
}