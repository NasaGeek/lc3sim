# create variable to identify resources
set(ResPath ${PROJECT_SOURCE_DIR}/core/res)
configure_file(common/paths.h ${PROJECT_SOURCE_DIR}/core/src/common/configured_paths.h)

# used findFLEX
find_package(BISON)
find_package(FLEX)

# generate c files for parser
file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/core/src/asm/parser_gen)
BISON_TARGET(Parser asm/lc3.y ${PROJECT_SOURCE_DIR}/core/src/asm/parser_gen/parser.cpp)
FLEX_TARGET(Scanner asm/lc3.l ${PROJECT_SOURCE_DIR}/core/src/asm/parser_gen/lexer.cpp)
ADD_FLEX_BISON_DEPENDENCY(Scanner Parser)

# mark files as generated
set_source_files_properties(${PROJECT_SOURCE_DIR}/core/src/asm/parser_gen/parser.cpp PROPERTIES GENERATED 1)
set_source_files_properties(${PROJECT_SOURCE_DIR}/core/src/asm/parser_gen/lexer.cpp PROPERTIES GENERATED 1)
set_source_files_properties(${PROJECT_SOURCE_DIR}/core/src/asm/parser_gen/parser.hpp PROPERTIES GENERATED 1)
set_source_files_properties(${PROJECT_SOURCE_DIR}/core/src/common/configured_paths.h PROPERTIES GENERATED 1)

include_directories(${PROJECT_SOURCE_DIR}/core/src/asm/parser_gen)  # need this because bison generates parser.hpp

# get all necessary files
file(GLOB CXX_SOURCES ${PROJECT_SOURCE_DIR}/core/src/common/*.cpp ${PROJECT_SOURCE_DIR}/core/src/asm/*.cpp ${PROJECT_SOURCE_DIR}/core/src/sim/*.cpp)
file(GLOB CXX_HEADERS ${PROJECT_SOURCE_DIR}/core/src/common/*.h ${PROJECT_SOURCE_DIR}/core/src/asm/*.h ${PROJECT_SOURCE_DIR}/core/src/sim/*.h)

add_custom_target(parser DEPENDS ${PROJECT_SOURCE_DIR}/core/src/asm/parser_gen/parser.cpp)

# generate library out of assembler
add_library(lc3core SHARED ${BISON_Parser_OUTPUTS} ${FLEX_Scanner_OUTPUTS} ${CXX_SOURCES} ${CXX_HEADERS})
target_link_libraries(lc3core jsonxx)
add_dependencies(lc3core parser)

add_subdirectory(thirdparty)
