import os
from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


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
        "yaml-cpp/0.8.0",
        "spdlog/1.13.0",
        "gtest/1.14.0",
        "xxhash/0.8.2",
    )

    def layout(self):
         # Базовая директория сборки
        self.folders.build = os.path.join("build", "conan")

        # Генераторы — внутри build, но в подпапке generators
        self.folders.generators = os.path.join(self.folders.build, "generators")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        toolchain = CMakeToolchain(self)
        toolchain.generate()
