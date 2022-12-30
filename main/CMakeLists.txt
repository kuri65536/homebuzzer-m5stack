set(srcs "main.c" "homebuzzer.cpp")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS ".")
