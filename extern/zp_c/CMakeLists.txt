find_package(OpenSSL REQUIRED COMPONENTS Crypto)

set(INC
  PUBLIC include
)

set(INC_SYS
)

set(SRC
  src/arena.c
  src/cli.c
  src/events.c
  src/fatal.c
  src/fs.c
  src/hash.c
  src/jobs.c
  src/json.c
  src/list.c
  src/local_ipc.c
  src/math.c
  src/memory.c
  src/rand.c
  src/repl.c
  src/shm.c
  src/str.c
  src/str_set.c
  src/uuid.c
)

set(LIB
  OpenSSL::Crypto
)

blender_add_lib(extern_zp_c "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
set_target_properties(extern_zp_c PROPERTIES C_STANDARD 23)
add_library(bf::extern::zp_c ALIAS extern_zp_c)
