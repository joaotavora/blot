from conan import ConanFile
from conan.tools.cmake import CMakeToolchain

class BlotConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "VirtualRunEnv"
    
    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = None
        tc.generate()
    def requirements(self):
        self.requires("boost/1.88.0")
        self.requires("fmt/11.2.0")
        self.requires("cli11/2.5.0")
        self.requires("re2/20240702")
    def build_requirements(self):
        self.requires("doctest/2.4.11")
        self.tool_requires("cmake/[>=3.30]")
    def layout(self):
        self.folders.generators = "conan" # put conan cruft here

