set(INC
  PUBLIC include
)

set(INC_SYS
)

set(SRC
  src/bb_archive.c
)

set(LIB
  PRIVATE bf::extern::zp_c
)

blender_add_lib(extern_bb_archive "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
set_target_properties(extern_bb_archive PROPERTIES C_STANDARD 23)
add_library(bf::extern::bb_archive ALIAS extern_bb_archive)
