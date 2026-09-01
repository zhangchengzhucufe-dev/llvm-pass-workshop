NumberExprAST表达式中的存数值

    VariableExprAST存表达式中的变量的名字

        BinaryExprAST存一个二元的表达式（左右值和运算符）

    CallExprAST存函数名和函数实体

        prototypeAST存函数名和变量名

            FunctionAST存prototype和函数体

                GetTokPrecedence获取运算符的优先级

                    ParseNumberExpr用当前的这样的NumVal 创建一个NumberExprAST并赋值出去

                        ParseParenExpr处理遇到括号的情况，吃掉括号和处理匹配报错

                            ParseExpression获取左值给到ParseBinOpRHS，让来比较优先级

                                ParsePrimary匹配curtok返回curtok的类型，并创建
                                    赋值出去改类型的节点

                                        ParseBinOpRHS传入LHS
                                            将获取的LHS获取他和他前面那个运算符的绑定值
                                                然后和他后面那个运算符的绑定值做比较，看看和谁绑定
