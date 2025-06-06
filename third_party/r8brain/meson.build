r8brain_lib = static_library(
    'r8brain',
    'src/r8bbase.cpp',
    include_directories : include_directories('src'),
    install : false
)

r8brain_dep = declare_dependency(
    include_directories : include_directories('src'),
    link_with : r8brain_lib
)