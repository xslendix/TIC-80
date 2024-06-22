################################
# ZIP
################################

set(CMAKE_DISABLE_TESTING ON CACHE BOOL "" FORCE)
add_subdirectory(${THIRDPARTY_DIR}/zip)
set_property(TARGET OBJLIB PROPERTY POSITION_INDEPENDENT_CODE 0)

