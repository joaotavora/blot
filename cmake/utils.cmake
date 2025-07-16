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