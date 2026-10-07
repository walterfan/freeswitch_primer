from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class FreeSwitchTutorialSiteConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = ()

    def requirements(self):
        self.requires("crowcpp-crow/1.2.1")
        self.requires("yaml-cpp/0.8.0")

    def configure(self):
        self.options["crowcpp-crow"].with_ssl = True

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeDeps(self).generate()
        CMakeToolchain(self).generate()
