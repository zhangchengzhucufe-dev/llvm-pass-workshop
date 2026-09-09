add_llvm_executable(mini-jit MiniJit.cpp)
llvm_map_components_to_libnames(JIT_LIBS Core Support OrcJIT native)
target_link_libraries(mini-jit PRIVATE ${JIT_LIBS})
