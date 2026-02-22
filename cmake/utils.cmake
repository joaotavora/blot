# Utility targets for development

# Code formatting with clang-format
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB_RECURSE ALL_SOURCE_FILES
    src/*.cpp src/*.hpp
    include/*.hpp
    test/*.cpp test/*.hpp
  )
  # Filter out test/fixture
  list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "test/fixture/.*")
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
    COMMENT "Formatting source code"
    VERBATIM
  )
endif()

# Regenerate fixture files
add_custom_target(regenerate-fixtures
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/util/regenerate-fixtures.sh
          ${CMAKE_CURRENT_BINARY_DIR}/blot
          test/fixture
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Regenerating fixture files"
  VERBATIM
)
