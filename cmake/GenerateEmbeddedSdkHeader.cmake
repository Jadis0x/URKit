if (NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "GenerateEmbeddedSdkHeader requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" SDK_HEADER_CONTENT)
set(DELIM "URKMODSDKH")
string(FIND "${SDK_HEADER_CONTENT}" ")${DELIM}\"" DELIM_POS)
if (NOT DELIM_POS EQUAL -1)
    message(FATAL_ERROR "Embedded SDK header delimiter collision")
endif()

file(WRITE "${OUTPUT}" "#pragma once\n\n#include <string_view>\n\nnamespace ModProjectGenerator {\ninline constexpr std::string_view kEmbeddedModSdkHeader = R\"${DELIM}(")
file(APPEND "${OUTPUT}" "${SDK_HEADER_CONTENT}")
file(APPEND "${OUTPUT}" ")${DELIM}\";\n} // namespace ModProjectGenerator\n")
