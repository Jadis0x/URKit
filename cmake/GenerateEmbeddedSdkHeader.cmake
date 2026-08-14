if (NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "GenerateEmbeddedSdkHeader requires INPUT and OUTPUT")
endif()

if (NOT DEFINED SYMBOL)
    set(SYMBOL kEmbeddedModSdkHeader)
endif()

if (NOT DEFINED DELIMITER)
    set(DELIMITER URKMODSDKH)
endif()

file(READ "${INPUT}" SDK_HEADER_CONTENT)
string(FIND "${SDK_HEADER_CONTENT}" ")${DELIMITER}\"" DELIM_POS)
if (NOT DELIM_POS EQUAL -1)
    message(FATAL_ERROR "Embedded SDK header delimiter collision")
endif()

file(WRITE "${OUTPUT}" "#pragma once\n\n#include <string_view>\n\nnamespace ModProjectGenerator {\ninline constexpr std::string_view ${SYMBOL} = R\"${DELIMITER}(")
file(APPEND "${OUTPUT}" "${SDK_HEADER_CONTENT}")
file(APPEND "${OUTPUT}" ")${DELIMITER}\";\n}\n")
