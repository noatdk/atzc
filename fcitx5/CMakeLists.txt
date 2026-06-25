# fcitx5 addon. Built from the top-level CMake only when Fcitx5Core is found.
find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)

add_library(fcitx5-atzc MODULE engine.cpp)
set_target_properties(fcitx5-atzc PROPERTIES OUTPUT_NAME atzc)  # -> libatzc.so
target_link_libraries(fcitx5-atzc PRIVATE Fcitx5::Core Fcitx5::Utils atzcclient)
target_include_directories(fcitx5-atzc PRIVATE ${PROJECT_SOURCE_DIR}/include)

install(TARGETS fcitx5-atzc DESTINATION "${FCITX_INSTALL_ADDONDIR}")
install(FILES atzc-addon.conf
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon" RENAME atzc.conf)
install(FILES atzc-im.conf
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod" RENAME atzc.conf)
