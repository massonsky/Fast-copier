from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class CcloneRecipe(ConanFile):
    name = "cclone"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    default_options = {
        "spdlog/*:header_only": True,
    }
    requires = (
        "cli11/2.4.2",
        "fmt/10.2.1",
        "spdlog/1.13.0",
        "gtest/1.14.0",
    )

    def layout(self):
        cmake_layout(self, build_folder="build/conan")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        toolchain = CMakeToolchain(self)
        toolchain.generate()
