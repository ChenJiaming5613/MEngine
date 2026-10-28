# Script mode: cmake -DIN_FILE=... -DOUT_FILE=... -P WrapSpirv.cmake
# slangc emits the array definition only; add an include guard and the headers its types need.

file(READ "${IN_FILE}" SPIRV_EMBEDDED)
string(STRIP "${SPIRV_EMBEDDED}" SPIRV_EMBEDDED)

file(WRITE "${OUT_FILE}"
    "// Generated from a Slang source file. Do not edit.\n"
    "#pragma once\n"
    "\n"
    "#include <cstddef>\n"
    "#include <cstdint>\n"
    "\n"
    "${SPIRV_EMBEDDED}\n"
)
