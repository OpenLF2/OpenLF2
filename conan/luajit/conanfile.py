"""Pinned LuaJIT rolling release, including upstream Windows ARM64 support."""

import os
import shlex
import subprocess

from conan import ConanFile
from conan.errors import ConanException
from conan.tools.files import chdir, copy, replace_in_file
from conan.tools.gnu import Autotools, AutotoolsToolchain
from conan.tools.layout import basic_layout
from conan.tools.microsoft import VCVars, is_msvc, unix_path
from conan.tools.scm import Git


class LuaJITConan(ConanFile):
    name = "luajit"
    version = "2.1.0-20260908"
    license = "MIT"
    homepage = "https://luajit.org/"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    # The upstream v2.1 rolling branch at the date in the Conan version.
    source_commit = "c6ffc141a8762b41703f9287d63d93622a13dd8f"

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        self.settings.rm_safe("compiler.cppstd")
        self.settings.rm_safe("compiler.libcxx")

    def layout(self):
        basic_layout(self, src_folder="src")

    def source(self):
        git = Git(self, folder=self.source_folder)
        git.run("init")
        git.run(f"fetch https://luajit.org/git/luajit.git {self.source_commit}")
        git.run("checkout --detach FETCH_HEAD")
        if git.get_commit() != self.source_commit:
            raise ConanException("LuaJIT source is not the pinned upstream commit")

    def generate(self):
        if is_msvc(self):
            VCVars(self).generate()
        else:
            toolchain = AutotoolsToolchain(self)
            environment = toolchain.environment()
            variables = environment.vars(self)
            cppflags = variables.get("CPPFLAGS") or ""
            if self.settings.os == "iOS":
                # LuaJIT compiles host generators as well as target objects. Conan's
                # iOS CFLAGS/LDFLAGS must never reach the macOS host executables.
                cflags = variables.get("CFLAGS") or ""
                ldflags = variables.get("LDFLAGS") or ""
                environment.unset("CFLAGS")
                environment.unset("LDFLAGS")
                environment.define("TARGET_CFLAGS", f"{cflags} {cppflags}".strip())
                environment.define("TARGET_LDFLAGS", ldflags)
                environment.define("TARGET_SHLDFLAGS", ldflags)
            elif cppflags:
                # LuaJIT's Makefile does not consume CPPFLAGS for target objects.
                environment.define("TARGET_CFLAGS", cppflags)
            toolchain.generate(environment)

    def _apple_make_args(self):
        if self.settings.os == "Macos":
            return ["DEFAULT_CC=clang", f"MACOSX_DEPLOYMENT_TARGET={self.settings.os.version}"]
        if self.settings.os != "iOS":
            return []
        sdk = str(self.settings.os.sdk)
        compiler = subprocess.check_output(["xcrun", "--sdk", sdk, "--find", "clang"], text=True).strip()
        strip = subprocess.check_output(["xcrun", "--sdk", sdk, "--find", "strip"], text=True).strip()
        host_compiler = subprocess.check_output(
            ["xcrun", "--sdk", "macosx", "--find", "clang"], text=True).strip()
        host_sdk = subprocess.check_output(
            ["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
        host_flags = f"-isysroot {shlex.quote(host_sdk)}"
        return ["DEFAULT_CC=clang", f"CROSS={os.path.dirname(compiler)}/",
                "TARGET_SYS=iOS", f"TARGET_STRIP={strip}",
                f"HOST_CC={host_compiler}", shlex.quote(f"HOST_CFLAGS={host_flags}"),
                shlex.quote(f"HOST_LDFLAGS={host_flags}")]

    def build(self):
        if is_msvc(self):
            with chdir(self, os.path.join(self.source_folder, "src")):
                self.run("msvcbuild.bat" if self.options.shared else "msvcbuild.bat static", env="conanbuild")
            if not os.path.isfile(os.path.join(self.source_folder, "src", "lua51.lib")):
                raise ConanException("LuaJIT MSVC build did not produce lua51.lib")
        else:
            if not self.options.shared:
                replace_in_file(self, os.path.join(self.source_folder, "src", "Makefile"),
                                "BUILDMODE= mixed", "BUILDMODE= static")
            with chdir(self, self.source_folder):
                Autotools(self).make(args=self._apple_make_args())

    def package(self):
        copy(self, "COPYRIGHT", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))
        if is_msvc(self):
            source = os.path.join(self.source_folder, "src")
            includedir = os.path.join(self.package_folder, "include", "luajit-2.1")
            for header in ("lua.h", "lualib.h", "lauxlib.h", "luaconf.h", "lua.hpp", "luajit.h"):
                copy(self, header, src=source, dst=includedir)
            copy(self, "lua51.lib", src=source, dst=os.path.join(self.package_folder, "lib"))
            if self.options.shared:
                copy(self, "lua51.dll", src=source, dst=os.path.join(self.package_folder, "bin"))
        else:
            with chdir(self, self.source_folder):
                Autotools(self).install(args=self._apple_make_args() +
                                        [f"PREFIX={unix_path(self, self.package_folder)}", "DESTDIR="])

    def package_info(self):
        self.cpp_info.libs = ["lua51" if is_msvc(self) else "luajit-5.1"]
        self.cpp_info.includedirs = [os.path.join("include", "luajit-2.1")]
        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs.extend(["m", "dl"])
