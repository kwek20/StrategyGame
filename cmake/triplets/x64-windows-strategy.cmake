set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Assimp registers both its obsolete glTF 1 importer and its glTF 2 importer for .glb. The glTF 1
# reader throws DeadlyImportError while probing every valid glTF 2 binary; Assimp catches it and
# then succeeds with glTF 2, but Visual Studio reports every throw as a first-chance exception.
# Disable only the legacy reader. ASSIMP_BUILD_NO_GLTF_IMPORTER would also remove glTF 2.
set(VCPKG_CXX_FLAGS "/DASSIMP_BUILD_NO_GLTF1_IMPORTER")
set(VCPKG_C_FLAGS "/DASSIMP_BUILD_NO_GLTF1_IMPORTER")
