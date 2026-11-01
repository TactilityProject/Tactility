"""Tests for tt.py against a local fake CDN. Run with: python3 -m unittest discover -s Tests/Python -p '*_test.py'"""

import functools
import hashlib
import http.server
import io
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
import zipfile

TT_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "tt.py"))
TOOLS_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Buildscripts", "TactilitySDK", "Tools"))
VERSION = "0.8.0-dev"

ECHO_TOOL = """import json, os, sys
import _bootstrap
_bootstrap.run()
print(json.dumps({"arguments": sys.argv[1:], "version": os.environ.get("TACTILITY_SDK_VERSION"), "path": os.environ.get("TACTILITY_SDK_PATH"), "cwd": os.getcwd()}))
sys.exit(3)
"""


def read_tool(file_name):
    with open(os.path.join(TOOLS_PATH, file_name)) as file:
        return file.read()


def make_zip(files):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, content in files.items():
            archive.writestr(name, content)
    return buffer.getvalue()


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class TtTest(unittest.TestCase):

    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.cdn = os.path.join(self.root, "cdn")
        self.home = os.path.join(self.root, "home")
        self.work = os.path.join(self.root, "work")
        os.makedirs(self.home)
        os.makedirs(self.work)
        self.publish_sdk(self.default_tools())
        handler = functools.partial(QuietHandler, directory=self.cdn)
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        shutil.rmtree(self.root)

    def default_tools(self):
        return {name: read_tool(name) for name in ("update-self.py", "update-sdk.py", "_bootstrap.py")} | {"echo.py": ECHO_TOOL}

    def publish_sdk(self, tools, commit="commit1", sdk_content="# sdk", platforms=("posix",)):
        version_directory = os.path.join(self.cdn, "sdk", VERSION)
        os.makedirs(version_directory, exist_ok=True)
        tools_zip = make_zip(tools)
        with open(os.path.join(version_directory, "tools.zip"), "wb") as file:
            file.write(tools_zip)
        for platform in platforms:
            with open(os.path.join(version_directory, f"TactilitySDK-{platform}.zip"), "wb") as file:
                file.write(make_zip({"TactilitySDK.cmake": sdk_content}))
        index = {
            "version": VERSION,
            "gitCommit": commit,
            "platforms": {platform: f"TactilitySDK-{platform}.zip" for platform in platforms},
            "tools": {"file": "tools.zip", "sha256": hashlib.sha256(tools_zip).hexdigest()},
        }
        with open(os.path.join(version_directory, "index.json"), "w") as file:
            json.dump(index, file)

    def write_manifest(self, directory, version=VERSION, platforms="posix"):
        os.makedirs(directory, exist_ok=True)
        with open(os.path.join(directory, "manifest.properties"), "w") as file:
            file.write(f"[target]\ntarget.sdk={version}\ntarget.platforms={platforms}\n")

    def run_tt(self, *arguments, tt_path=TT_PATH, environment=None, input_text=""):
        extra_environment = environment or {}
        environment = dict(os.environ)
        environment.pop("TACTILITY_SDK_PATH", None)
        environment.update(extra_environment)
        environment["HOME"] = self.home
        environment["TT_CDN"] = f"http://127.0.0.1:{self.server.server_address[1]}"
        environment.pop("TACTILITY_SDK_VERSION", None)
        return subprocess.run([sys.executable, tt_path, *arguments], cwd=self.work, env=environment, capture_output=True, text=True, timeout=60, input=input_text)

    def sdk_path(self, *parts):
        return os.path.join(self.home, ".tactility", "sdk", VERSION, *parts)

    def expire_index(self):
        os.utime(self.sdk_path("index.json"), (0, 0))

    def test_forwards_arguments_exit_code_and_environment(self):
        self.write_manifest(self.work)
        result = self.run_tt("echo", "a", "b c")
        self.assertEqual(result.returncode, 3, result.stderr)
        output = json.loads(result.stdout)
        self.assertEqual(output["arguments"], ["a", "b c"])
        self.assertEqual(output["version"], VERSION)
        self.assertEqual(output["path"], os.path.join(self.home, ".tactility"))
        self.assertEqual(os.path.realpath(output["cwd"]), os.path.realpath(self.work))
        self.assertTrue(os.path.isfile(self.sdk_path("platform-posix", "TactilitySDK", "TactilitySDK.cmake")))
        self.assertTrue(os.path.isfile(self.sdk_path("tools", "echo.py")))

    def test_current_directory_beats_apps_directories(self):
        self.write_manifest(self.work)
        self.write_manifest(os.path.join(self.work, "Apps", "a"), version="9.9.9")
        self.assertEqual(self.run_tt("echo").returncode, 3)

    def test_apps_directory_beats_lowercase_apps_and_sorts(self):
        self.write_manifest(os.path.join(self.work, "Apps", "b"), version="9.9.9")
        self.write_manifest(os.path.join(self.work, "Apps", "a"))
        self.write_manifest(os.path.join(self.work, "apps", "a"), version="9.9.9")
        self.assertEqual(self.run_tt("echo").returncode, 3)

    def test_lowercase_apps_directory(self):
        self.write_manifest(os.path.join(self.work, "apps", "a"))
        self.assertEqual(self.run_tt("echo").returncode, 3)

    def test_arguments_are_checked_before_the_manifest(self):
        shutil.rmtree(self.cdn)
        result = self.run_tt("help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Usage: tt", result.stdout)
        result = self.run_tt("../x")
        self.assertEqual(result.returncode, 1)
        self.assertIn("command not found: ../x", result.stderr)
        self.assertNotIn("manifest", result.stderr)

    def test_missing_manifest_fails(self):
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no manifest.properties found", result.stderr)

    def publish_new_project(self, files=None):
        files = files or {
            "gui/manifest.properties": "target.sdk=@SDK_VERSION@\nid=@PACKAGE_ID@\napp.0.id=@APP_ID@\napp.0.name=@APP_NAME@\n",
            "gui/main/Source/main.c": "// gui\n",
            "terminal/manifest.properties": "target.sdk=@SDK_VERSION@\nid=@PACKAGE_ID@\napp.0.id=@APP_ID@\napp.0.name=@APP_NAME@\napp.0.headless=true\n",
            "terminal/main/Source/main.c": "// terminal\n",
        }
        tools = self.default_tools() | {"app.py": read_tool("app.py"), "_device_host.py": read_tool("_device_host.py")}
        tools |= {f"NewProject/{name}": content for name, content in files.items()}
        self.publish_sdk(tools)

    def read_work_file(self, *parts):
        with open(os.path.join(self.work, *parts)) as file:
            return file.read()

    def test_app_create_uses_defaults_and_the_gui_variant(self):
        self.publish_new_project()
        result = self.run_tt("app", "create", input_text=f"{VERSION}\n\n\n\n\n")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(self.read_work_file("manifest.properties"), f"target.sdk={VERSION}\nid=example.helloworld\napp.0.id=helloworld\napp.0.name=Hello World\n")
        self.assertEqual(self.read_work_file("main", "Source", "main.c"), "// gui\n")

    def test_app_create_with_answers_and_the_terminal_variant(self):
        self.publish_new_project()
        result = self.run_tt("app", "create", input_text=f"{VERSION}\nterminal\nmy.package\nmyapp\nMy App\n")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(self.read_work_file("manifest.properties"), f"target.sdk={VERSION}\nid=my.package\napp.0.id=myapp\napp.0.name=My App\napp.0.headless=true\n")
        self.assertEqual(self.read_work_file("main", "Source", "main.c"), "// terminal\n")

    def test_app_create_module_variant_skips_the_app_questions(self):
        self.publish_new_project(files={
            "gui/manifest.properties": "gui\n",
            "module/manifest.properties": "target.sdk=@SDK_VERSION@\nid=@PACKAGE_ID@\nmodule.0.binary=main\n",
            "module/main/Source/main.c": "// module\n",
        })
        result = self.run_tt("app", "create", input_text=f"{VERSION}\nmodule\nmy.package\n")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(self.read_work_file("manifest.properties"), f"target.sdk={VERSION}\nid=my.package\nmodule.0.binary=main\n")
        self.assertEqual(self.read_work_file("main", "Source", "main.c"), "// module\n")
        self.assertNotIn("App id", result.stdout)

    def test_app_create_asks_again_for_invalid_answers(self):
        self.publish_new_project()
        result = self.run_tt("app", "create", input_text=f"..\n{VERSION}\nplugin\n\nab\nmy.package\n\nM\nMy App\n")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("invalid SDK version", result.stderr)
        self.assertIn("Use gui, terminal or module", result.stdout)
        self.assertIn("Use 5 to 32 letters, digits and dots", result.stdout)
        self.assertIn("Use 2 to 32 letters, digits, spaces and dashes", result.stdout)
        self.assertIn("id=my.package", self.read_work_file("manifest.properties"))

    def test_app_create_needs_the_sdk_to_have_templates(self):
        self.publish_new_project(files={"other/manifest.properties": "x\n"})
        result = self.run_tt("app", "create", input_text=f"{VERSION}\n\n\n\n\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("has no gui project template", result.stdout)

    def test_app_create_does_not_overwrite(self):
        self.publish_new_project()
        os.makedirs(os.path.join(self.work, "main"))
        result = self.run_tt("app", "create", input_text=f"{VERSION}\n\n\n\n\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("already has", result.stdout)

    def test_host_is_built_in_and_needs_no_sdk(self):
        shutil.rmtree(self.cdn)
        result = self.run_tt("host")
        self.assertEqual((result.returncode, result.stdout.strip()), (0, "none"), result.stderr)
        result = self.run_tt("host", "127.0.0.1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Device host: 127.0.0.1", result.stdout)
        self.assertEqual(self.read_work_file(".tactility", "device_host").strip(), "127.0.0.1")
        result = self.run_tt("host")
        self.assertEqual(result.stdout.strip(), "127.0.0.1")
        result = self.run_tt("host", "a", "b")
        self.assertEqual(result.returncode, 1)
        self.assertIn("usage: tt host", result.stderr)

    def test_sdk_is_built_in_and_stores_the_version(self):
        shutil.rmtree(self.cdn)
        result = self.run_tt("sdk")
        self.assertEqual((result.returncode, result.stdout.strip()), (0, "none"), result.stderr)
        result = self.run_tt("sdk", "1.2.3")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.read_work_file(".tactility", "sdk_version").strip(), "1.2.3")
        self.assertEqual(self.run_tt("sdk").stdout.strip(), f"1.2.3 (from local setting {os.path.join('.tactility', 'sdk_version')})")
        self.write_manifest(self.work)
        output = self.run_tt("sdk").stdout
        self.assertIn(f"{VERSION} (from project manifest.properties)", output)
        self.assertIn("1.2.3", output)
        self.assertIn("not used", output)
        os.remove(os.path.join(self.work, "manifest.properties"))
        for arguments in (("sdk", "../x"), ("sdk", "1", "2")):
            result = self.run_tt(*arguments)
            self.assertEqual(result.returncode, 1)
            self.assertIn("usage: tt sdk", result.stderr)

    def test_sdk_shows_the_device_version_when_it_leads(self):
        import contextlib
        import importlib.util
        spec = importlib.util.spec_from_file_location("tt_module", TT_PATH)
        tt = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tt)
        tt.query_device_info = lambda host: {"vendor": "V", "name": "N", "id": "i", "version": "4.5.6"}
        previous = os.getcwd()
        os.chdir(self.work)
        try:
            tt.write_stored(tt.DEVICE_HOST_PATH, "10.0.0.2")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                tt.sdk_command([])
            self.assertEqual(output.getvalue().strip(), "4.5.6 (from device 10.0.0.2)")
            tt.write_stored(tt.SDK_VERSION_PATH, "1.2.3")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                tt.sdk_command([])
            self.assertIn("1.2.3 (from local setting", output.getvalue())
        finally:
            os.chdir(previous)

    def test_host_does_not_query_the_old_host(self):
        shutil.rmtree(self.cdn)
        self.run_tt("host", "192.0.2.1")
        result = self.run_tt("host", "127.0.0.1")
        self.assertNotIn("192.0.2.1", result.stderr)

    def test_without_a_project_the_sdk_version_can_be_given(self):
        result = self.run_tt("echo", "a", input_text=f"{VERSION}\n")
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(json.loads(result.stdout)["version"], VERSION)
        self.assertFalse(os.path.exists(os.path.join(self.work, "manifest.properties")))

    def test_chosen_sdk_version_is_stored_in_the_current_folder(self):
        result = self.run_tt("echo", input_text=f"{VERSION}\n")
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(self.read_work_file(".tactility", "sdk_version").strip(), VERSION)
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(json.loads(result.stdout)["version"], VERSION)
        self.assertNotIn("SDK version to use", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.home, ".tactility", "sdk_version")))

    def test_project_sdk_beats_the_stored_sdk_version(self):
        os.makedirs(os.path.join(self.work, ".tactility"))
        with open(os.path.join(self.work, ".tactility", "sdk_version"), "w") as file:
            file.write("9.9.9\n")
        self.write_manifest(self.work)
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(json.loads(result.stdout)["version"], VERSION)
        self.assertNotIn("9.9.9", result.stderr)

    def test_without_a_project_and_version_it_fails(self):
        result = self.run_tt("echo", input_text="\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no manifest.properties found", result.stderr)

    def test_unknown_and_invalid_commands_are_not_found(self):
        self.write_manifest(self.work)
        for command in ("nope", "../echo", "Echo"):
            result = self.run_tt(command)
            self.assertEqual(result.returncode, 1)
            self.assertIn(f"command not found: {command}", result.stderr)

    def test_help_lists_commands(self):
        self.write_manifest(self.work)
        result = self.run_tt()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("echo", result.stdout)
        self.assertIn(VERSION, result.stdout)

    def test_installed_tools_and_platforms_are_only_replaced_by_update_sdk(self):
        self.write_manifest(self.work)
        self.assertEqual(self.run_tt("echo").returncode, 3)
        tools = self.default_tools() | {"echo.py": "import sys\nimport _bootstrap\n_bootstrap.run()\nsys.exit(4)\n"}
        self.publish_sdk(tools, commit="commit2", sdk_content="# sdk 2")
        self.expire_index()
        self.expire_update_check()
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 3)
        self.assertEqual(self.read_sdk_file(), "# sdk")
        self.assertIn("run 'tt update-sdk'", result.stderr)
        result = self.run_tt("update-sdk")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Downloading the tools", result.stdout)
        self.assertIn("was updated", result.stdout)
        self.assertEqual(self.run_tt("echo").returncode, 4)
        self.assertEqual(self.read_sdk_file(), "# sdk 2")

    def test_all_platforms_installs_every_platform_of_the_sdk(self):
        self.publish_sdk(self.default_tools(), platforms=("posix", "esp32"))
        self.write_manifest(self.work, platforms="all")
        self.assertEqual(self.run_tt("echo").returncode, 3)
        for platform in ("posix", "esp32"):
            self.assertTrue(os.path.isdir(self.sdk_path(f"platform-{platform}", "TactilitySDK")), platform)

    def test_missing_platform_of_a_republished_sdk_asks_for_update_sdk(self):
        self.publish_sdk(self.default_tools(), platforms=("posix", "esp32"))
        self.write_manifest(self.work)
        self.assertEqual(self.run_tt("echo").returncode, 3)
        self.publish_sdk(self.default_tools(), commit="commit2", sdk_content="# sdk 2", platforms=("posix", "esp32"))
        self.expire_index()
        self.write_manifest(self.work, platforms="posix,esp32")
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 1)
        self.assertIn("run 'tt update-sdk' first", result.stderr)
        self.assertFalse(os.path.exists(self.sdk_path("platform-esp32")))
        self.assertEqual(self.run_tt("update-sdk").returncode, 0)
        self.assertEqual(self.run_tt("echo").returncode, 3)
        for platform in ("posix", "esp32"):
            with open(self.sdk_path(f"platform-{platform}", "commit")) as file:
                self.assertEqual(file.read(), "commit2", platform)

    def test_requested_platform_that_the_index_no_longer_has_is_refused(self):
        self.publish_sdk(self.default_tools(), platforms=("posix", "esp32"))
        self.write_manifest(self.work, platforms="posix")
        self.assertEqual(self.run_tt("echo").returncode, 3)
        self.publish_sdk(self.default_tools(), commit="commit2", sdk_content="# sdk 2", platforms=("esp32", "other"))
        self.expire_index()
        self.write_manifest(self.work, platforms="posix,other")
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 1)
        self.assertIn("platform posix is not available", result.stderr)

    def test_failed_update_sdk_keeps_the_installed_build(self):
        self.write_manifest(self.work)
        self.assertEqual(self.run_tt("echo").returncode, 3)
        tools = self.default_tools() | {"echo.py": "import sys\nsys.exit(4)\n"}
        self.publish_sdk(tools, commit="commit2", sdk_content="# sdk 2")
        os.remove(os.path.join(self.cdn, "sdk", VERSION, "TactilitySDK-posix.zip"))
        result = self.run_tt("update-sdk")
        self.assertEqual(result.returncode, 1)
        self.assertIn("was not changed", result.stderr)
        self.assertEqual(self.read_sdk_file(), "# sdk")
        self.assertEqual(self.run_tt("echo").returncode, 3)
        self.assertEqual(sorted(name for name in os.listdir(self.sdk_path()) if name.endswith((".new", ".old"))), [])

    def test_installed_sdk_works_offline(self):
        self.write_manifest(self.work)
        self.assertEqual(self.run_tt("echo").returncode, 3)
        shutil.rmtree(self.cdn)
        self.expire_index()
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 3, result.stderr)

    def test_tools_with_a_wrong_checksum_are_refused(self):
        self.write_manifest(self.work)
        with open(os.path.join(self.cdn, "sdk", VERSION, "tools.zip"), "wb") as file:
            file.write(make_zip({"echo.py": "tampered"}))
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 1)
        self.assertIn("checksum", result.stderr)

    def test_local_uses_tactility_sdk_path(self):
        self.write_manifest(self.work)
        local_path = os.path.join(self.root, "local")
        os.makedirs(os.path.join(local_path, "sdk", VERSION, "tools"))
        for name, content in self.default_tools().items():
            with open(os.path.join(local_path, "sdk", VERSION, "tools", name), "w") as file:
                file.write(content)
        shutil.rmtree(self.cdn)
        result = self.run_tt("echo", "a", environment={"TACTILITY_SDK_PATH": local_path})
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIn(f"using the local SDK at {local_path}", result.stderr)
        output = json.loads(result.stdout)
        self.assertEqual(output["arguments"], ["a"])
        self.assertEqual(output["path"], local_path)
        self.assertEqual(output["version"], VERSION)
        self.assertFalse(os.path.exists(self.sdk_path()))

    def test_empty_tactility_sdk_path_counts_as_unset(self):
        self.write_manifest(self.work)
        result = self.run_tt("echo", environment={"TACTILITY_SDK_PATH": ""})
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertNotIn("local SDK", result.stderr)
        self.assertEqual(json.loads(result.stdout)["path"], os.path.join(self.home, ".tactility"))

    def test_local_tools_without_tactility_sdk_path_are_not_used(self):
        self.write_manifest(self.work)
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertNotIn("local SDK", result.stderr)
        self.assertEqual(json.loads(result.stdout)["path"], os.path.join(self.home, ".tactility"))

    def publish_tt(self, files):
        with open(os.path.join(self.cdn, "sdk", "tt.zip"), "wb") as file:
            file.write(make_zip(files))
        with open(os.path.join(self.cdn, "sdk", "tt.json"), "w") as file:
            json.dump({"version": "1.0.1"}, file)

    def test_update_replaces_itself_and_installs_wrappers(self):
        tt_copy = os.path.join(self.root, "tt.py")
        shutil.copy(TT_PATH, tt_copy)
        with open(os.path.join(self.root, "tt.cmd"), "w") as file:
            file.write("@echo off\r\n")
        self.publish_tt({"tt.py": "# updated\n", "tt": "#!/bin/sh\n", "tt.ps1": "exit 0\r\n"})
        self.write_manifest(self.work)
        result = self.run_tt("update-self", tt_path=tt_copy)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(tt_copy) as file:
            self.assertEqual(file.read(), "# updated\n")
        self.assertTrue(os.access(os.path.join(self.root, "tt"), os.X_OK))
        self.assertTrue(os.access(os.path.join(self.root, "tt.ps1"), os.X_OK))
        self.assertTrue(os.access(tt_copy, os.X_OK))
        # The obsolete tt.cmd is removed
        self.assertEqual(sorted(os.listdir(self.root)), ["cdn", "home", "tt", "tt.ps1", "tt.py", "work"])

    def test_update_rejects_invalid_python(self):
        tt_copy = os.path.join(self.root, "tt.py")
        shutil.copy(TT_PATH, tt_copy)
        self.publish_tt({"tt.py": "def broken(:\n", "tt": "#!/bin/sh\n"})
        self.write_manifest(self.work)
        result = self.run_tt("update-self", tt_path=tt_copy)
        self.assertEqual(result.returncode, 1)
        with open(tt_copy) as file, open(TT_PATH) as original:
            self.assertEqual(file.read(), original.read())
        self.assertEqual(sorted(os.listdir(self.root)), ["cdn", "home", "tt.py", "work"])

    def test_update_rejects_unsafe_file_names(self):
        tt_copy = os.path.join(self.root, "tt.py")
        shutil.copy(TT_PATH, tt_copy)
        self.publish_tt({"tt.py": "# updated\n", "../evil": "x"})
        self.write_manifest(self.work)
        result = self.run_tt("update-self", tt_path=tt_copy)
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid", result.stderr)

    def test_wrapper_runs_tt(self):
        self.write_manifest(self.work)
        wrapper = os.path.join(os.path.dirname(TT_PATH), "tt")
        environment = dict(os.environ, HOME=self.home, TT_CDN=f"http://127.0.0.1:{self.server.server_address[1]}")
        result = subprocess.run([wrapper, "echo", "x"], cwd=self.work, env=environment, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(json.loads(result.stdout)["arguments"], ["x"])

    def publish_tt_version(self, version):
        with open(os.path.join(self.cdn, "sdk", "tt.json"), "w") as file:
            json.dump({"version": version}, file)

    def test_bootstrap_suggests_update(self):
        self.write_manifest(self.work)
        self.publish_tt_version("99.0.0")
        result = self.run_tt("echo")
        self.assertEqual(result.returncode, 3)
        self.assertIn("version 99.0.0 is available", result.stderr)
        self.assertIn("tt update-self", result.stderr)

    def test_bootstrap_checks_at_most_every_30_minutes(self):
        self.write_manifest(self.work)
        self.publish_tt_version("1.0.0")
        self.assertNotIn("is available", self.run_tt("echo").stderr)
        self.publish_tt_version("99.0.0")
        self.assertNotIn("is available", self.run_tt("echo").stderr)
        state_path = os.path.join(self.home, ".tactility", "tt.json")
        with open(state_path) as file:
            state = json.load(file)
        state["checked"] -= 31 * 60
        with open(state_path, "w") as file:
            json.dump(state, file)
        self.assertIn("version 99.0.0 is available", self.run_tt("echo").stderr)

    def expire_update_check(self):
        state_path = os.path.join(self.home, ".tactility", "tt.json")
        with open(state_path) as file:
            state = json.load(file)
        state["checked"] -= 31 * 60
        with open(state_path, "w") as file:
            json.dump(state, file)

    def read_sdk_file(self):
        with open(self.sdk_path("platform-posix", "TactilitySDK", "TactilitySDK.cmake")) as file:
            return file.read()

    def test_newer_sdk_build_is_suggested_and_installed_by_update_sdk(self):
        self.write_manifest(self.work)
        self.assertNotIn("update-sdk", self.run_tt("echo").stderr)
        self.publish_sdk(self.default_tools(), commit="commit2", sdk_content="# sdk 2")
        self.assertNotIn("update-sdk", self.run_tt("echo").stderr)
        self.expire_update_check()
        self.assertIn("run 'tt update-sdk'", self.run_tt("echo").stderr)
        self.assertEqual(self.read_sdk_file(), "# sdk")
        result = self.run_tt("update-sdk")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"SDK {VERSION} was updated", result.stdout)
        self.assertEqual(self.read_sdk_file(), "# sdk 2")
        self.assertNotIn("update-sdk", self.run_tt("echo").stderr)
        self.assertIn("is up to date", self.run_tt("update-sdk").stdout)

    def test_local_sdk_is_not_updated(self):
        self.write_manifest(self.work)
        local_path = os.path.join(self.root, "local")
        os.makedirs(os.path.join(local_path, "sdk", VERSION, "platform-posix", "TactilitySDK"))
        os.makedirs(os.path.join(local_path, "sdk", VERSION, "tools"))
        for name, content in self.default_tools().items():
            with open(os.path.join(local_path, "sdk", VERSION, "tools", name), "w") as file:
                file.write(content)
        result = self.run_tt("echo", environment={"TACTILITY_SDK_PATH": local_path})
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertNotIn("update-sdk", result.stderr)
        result = self.run_tt("update-sdk", environment={"TACTILITY_SDK_PATH": local_path})
        self.assertEqual(result.returncode, 1)
        self.assertIn("local SDK", result.stderr)

    def test_internal_tools_are_not_commands(self):
        self.write_manifest(self.work)
        result = self.run_tt("_bootstrap")
        self.assertEqual(result.returncode, 1)
        self.assertIn("command not found", result.stderr)
        self.assertNotIn("_bootstrap", self.run_tt("help").stdout)

    def publish_simulator(self, simulator_platform, sha256=None):
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w") as zip_file:
            zip_file.writestr("Tactility", "#!/bin/sh\necho simulator \"$@\"\n")
        content = archive.getvalue()
        version_directory = os.path.join(self.cdn, "sdk", VERSION)
        file_name = f"Simulator-{simulator_platform}.zip"
        with open(os.path.join(version_directory, file_name), "wb") as file:
            file.write(content)
        index_path = os.path.join(version_directory, "index.json")
        with open(index_path) as file:
            index = json.load(file)
        index["simulators"] = {simulator_platform: {"file": file_name, "sha256": sha256 or hashlib.sha256(content).hexdigest()}}
        with open(index_path, "w") as file:
            json.dump(index, file)

    @unittest.skipUnless(sys.platform in ("linux", "darwin"), "the simulator only exists for Linux and macOS")
    def test_simulator_downloads_and_runs_the_simulator_of_this_computer(self):
        host_platform = f"{'macos' if sys.platform == 'darwin' else 'linux'}-{platform.machine()}"
        self.publish_sdk(self.default_tools() | {"simulator.py": read_tool("simulator.py")})
        self.publish_simulator(host_platform)
        self.write_manifest(self.work)
        result = self.run_tt("simulator", "a", "b")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "simulator a b")
        self.assertTrue(os.path.isfile(self.sdk_path(f"simulator-{host_platform}", "Tactility")))
        self.assertIn("Downloading the simulator", result.stderr)
        # An installed simulator of the current build isn't downloaded again
        result = self.run_tt("simulator")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Downloading the simulator", result.stderr)

    @unittest.skipUnless(sys.platform in ("linux", "darwin"), "the simulator only exists for Linux and macOS")
    def test_installed_simulator_runs_offline(self):
        host_platform = f"{'macos' if sys.platform == 'darwin' else 'linux'}-{platform.machine()}"
        self.publish_sdk(self.default_tools() | {"simulator.py": read_tool("simulator.py")})
        self.publish_simulator(host_platform)
        self.write_manifest(self.work)
        self.assertEqual(self.run_tt("simulator").returncode, 0)
        shutil.rmtree(self.cdn)
        self.expire_index()
        result = self.run_tt("simulator", "a")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "simulator a")

    @unittest.skipUnless(sys.platform in ("linux", "darwin"), "the simulator only exists for Linux and macOS")
    def test_installed_simulator_runs_when_the_download_fails(self):
        host_platform = f"{'macos' if sys.platform == 'darwin' else 'linux'}-{platform.machine()}"
        self.publish_sdk(self.default_tools() | {"simulator.py": read_tool("simulator.py")})
        self.publish_simulator(host_platform)
        self.write_manifest(self.work)
        self.assertEqual(self.run_tt("simulator").returncode, 0)
        with open(self.sdk_path(f"simulator-{host_platform}", "commit"), "w") as file:
            file.write("older")
        os.remove(os.path.join(self.cdn, "sdk", VERSION, f"Simulator-{host_platform}.zip"))
        result = self.run_tt("simulator", "a")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "simulator a")

    @unittest.skipUnless(sys.platform in ("linux", "darwin"), "the simulator only exists for Linux and macOS")
    def test_simulator_refuses_a_download_that_doesnt_match_its_checksum(self):
        host_platform = f"{'macos' if sys.platform == 'darwin' else 'linux'}-{platform.machine()}"
        self.publish_sdk(self.default_tools() | {"simulator.py": read_tool("simulator.py")})
        self.publish_simulator(host_platform, sha256="0" * 64)
        self.write_manifest(self.work)
        result = self.run_tt("simulator")
        self.assertEqual(result.returncode, 1)
        self.assertIn("checksum", result.stderr)
        self.assertFalse(os.path.exists(self.sdk_path(f"simulator-{host_platform}")))

    def test_simulator_reports_a_missing_platform(self):
        self.publish_sdk(self.default_tools() | {"simulator.py": read_tool("simulator.py")})
        self.publish_simulator("other-platform")
        self.write_manifest(self.work)
        result = self.run_tt("simulator")
        self.assertEqual(result.returncode, 1)
        self.assertIn("other-platform", result.stderr)


if __name__ == "__main__":
    unittest.main()
