set(VCPKG_BUILD_TYPE release)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO julixian/tomlkit11
    REF "v${VERSION}"
    SHA512 0af94a297dec919e82b85d0e11bbbfe119daecb2a2f5991103b7ccbc91efe1e63ea670418078cff67a6314e2f130acfb4224b0a3780adbc63e67b5fef7cecd23
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DTOMLKIT11_BUILD_TESTS=OFF
        -DTOMLKIT11_BUILD_TOOLS=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH share/cmake/tomlkit11)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
