add_executable(libtool
  libtool.cc)
target_compile_definitions(libtool PRIVATE
  ${LLVM_DEFINITIONS})
target_compile_options(libtool PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/EHsc- /GR->
  $<$<CXX_COMPILER_ID:AppleClang>:-fno-exceptions -fno-rtti>
  $<$<CXX_COMPILER_ID:Clang>:-fno-exceptions -fno-rtti>
  $<$<CXX_COMPILER_ID:GNU>:-fno-exceptions -fno-rtti>)
target_include_directories(libtool PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS})
target_link_libraries(libtool PRIVATE
  clangRewriteFrontend
  clangTooling)
