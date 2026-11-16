set(VCPKG_BUILD_TYPE release)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO julixian/tomlkit11
    REF "v${VERSION}"
    SHA512 4f0cd42a0110320a290c2071086cb49e8b75b660b0aea6a56480bdb4a638bc561df6f44ede5c36300610945304c5a6e7a2c3286c7ad94333983ea9d600f62d75
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

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
