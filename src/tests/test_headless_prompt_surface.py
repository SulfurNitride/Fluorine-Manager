#!/usr/bin/env python3

from pathlib import Path
import re
import unittest


SOURCE_DIR = Path(__file__).resolve().parents[2] / "src/src"


def function_body(source: str, signature_regex: str) -> str:
    """Return the brace-matched body of the first function matching the regex."""
    match = re.search(signature_regex, source)
    if match is None:
        raise AssertionError(f"no function matches {signature_regex!r}")
    start = source.index("{", match.end())
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unbalanced braces after {signature_regex!r}")


class HeadlessPromptSurfaceTests(unittest.TestCase):
    def test_process_runner_has_headless_wait(self) -> None:
        header = (SOURCE_DIR / "processrunner.h").read_text(encoding="utf-8")
        source = (SOURCE_DIR / "processrunner.cpp").read_text(encoding="utf-8")
        self.assertIn("ProcessRunner& setHeadless(bool headless);", header)
        self.assertIn("bool m_headless = false;", header)

        post_run = function_body(source, r"ProcessRunner::Results ProcessRunner::postRun\(\)")
        branch = re.search(r"if \(m_headless\)\s*\{", post_run)
        self.assertIsNotNone(branch, "postRun has no m_headless branch")
        branch_body = function_body(post_run[branch.start() :], r"if \(m_headless\)")
        self.assertIn("env::waitForProcessTree", branch_body)
        self.assertNotIn("withLock", branch_body)
        self.assertIn("headless::setWaitActive(true)", branch_body)
        self.assertIn("headless::waitAction()", branch_body)

        run_binary = function_body(source, r"ProcessRunner::runBinary\(\)")
        self.assertRegex(run_binary, r"!m_headless\s*&&[^;]*SteamCloud::supported")

    def _read(self, name: str) -> str:
        return (SOURCE_DIR / name).read_text(encoding="utf-8")

    def test_run_declares_headless_flag(self) -> None:
        source = self._read("commandline.cpp")
        body = function_body(source, r"RunCommand::getVisibleOptions\(\) const")
        self.assertRegex(
            body,
            r'"headless",\s*po::value<bool>\(\)->default_value\(false\)->zero_tokens\(\)',
        )
        header = self._read("commandline.h")
        self.assertIn("virtual bool headless() const", header)
        self.assertIn("bool headless() const;", header)
        self.assertRegex(
            function_body(source, r"bool RunCommand::headless\(\) const"),
            r'vm\(\)\["headless"\]',
        )
        self.assertRegex(
            function_body(source, r"bool CommandLine::headless\(\) const"),
            r"m_command\s*&&\s*m_command->headless\(\)",
        )

    def _ephemeral_branch(self) -> str:
        source = self._read("main.cpp")
        return function_body(source, r"if \(multiProcess\.ephemeral\(\)\)")

    def test_main_refuses_instead_of_forwarding(self) -> None:
        branch = self._ephemeral_branch()
        self.assertIn("cl.headless()", branch)
        self.assertLess(branch.index("cl.headless()"), branch.index("forwardToPrimary"))

    def test_main_ignores_multiple_when_headless(self) -> None:
        self.assertIn(
            "MOMultiProcess multiProcess(cl.multiple() && !cl.headless());",
            self._read("main.cpp"),
        )

    def test_main_checks_headless_primary_before_forwarding(self) -> None:
        branch = self._ephemeral_branch()
        self.assertIn("primaryIsHeadless()", branch)
        self.assertLess(
            branch.index("primaryIsHeadless()"), branch.index("forwardToPrimary")
        )

    def test_headless_primary_drops_external_messages(self) -> None:
        body = function_body(
            self._read("moapplication.cpp"),
            r"void MOApplication::externalMessage\(const QString& message\)",
        )
        guard = re.search(r"if \(headless::active\(\)\)\s*\{[^}]*return;", body)
        self.assertIsNotNone(guard, "externalMessage does not return on headless::active()")
        self.assertLess(guard.start(), body.index("MOShortcut"))

    def test_run_still_forwards_without_headless(self) -> None:
        body = function_body(
            self._read("commandline.cpp"), r"bool RunCommand::canForwardToPrimary\(\) const"
        )
        self.assertRegex(body, r"return\s*!headless\(\);")

    def test_thread_start_never_reinstalls_term_handlers(self) -> None:
        source = self._read("main.cpp")
        body = function_body(source, r"void setExceptionHandlers\(\)\s*(?=\{)")
        self.assertNotIn("SIGTERM", body)
        self.assertNotIn("SIGINT", body)
        term = function_body(source, r"void setTermHandlers\(\)\s*(?=\{)")
        self.assertIn("SIGTERM", term)
        self.assertIn("SIGINT", term)
        run = function_body(source, r"int run\(int argc, char\* argv\[\]\)\s*(?=\{)")
        self.assertIn("setTermHandlers();", run)

    def test_run_checks_named_profile(self) -> None:
        body = function_body(
            self._read("commandline.cpp"),
            r"RunCommand::runPostOrganizer\(OrganizerCore& core\)",
        )
        self.assertIn('vm().count("profile")', body)
        self.assertRegex(body, r"currentProfile\(\)")
        self.assertIn("->name()", body)

    def _body_of(self, name: str, regex: str) -> str:
        return function_body(self._read(name), regex)

    def _assert_before(self, body: str, first: str, second: str) -> None:
        self.assertIn(first, body)
        self.assertIn(second, body)
        self.assertLess(body.index(first), body.index(second))

    def test_instance_resolution_never_picks_in_headless(self) -> None:
        body = self._body_of(
            "moapplication.cpp", r"MOApplication::getCurrentInstance\(bool forceSelect\)"
        )
        self.assertIn("headless::fail(", body)
        self.assertIn("Instance '%1' not found.", body)
        self._assert_before(body, "headless::active()", "clearOverrides()")
        self._assert_before(body, "headless::active()", "selectInstance()")
        select = self._body_of("instancemanager.cpp", r"std::unique_ptr<Instance> selectInstance\(\)")
        self.assertRegex(select, r"^\{\s*if \(headless::active\(\)\)\s*\{\s*return \{\};")
        self.assertLess(select.index("headless::active()"), select.index("PluginContainer"))

    def test_setup_instance_never_opens_game_or_variant_pickers(self) -> None:
        body = self._body_of(
            "instancemanager.cpp",
            r"SetupInstanceResults setupInstance\(Instance& instance, PluginContainer& pc\)",
        )
        for case, picker in (
            ("IniMissingGame", "selectGame"),
            ("GameGone", "selectGame"),
            ("MissingVariant", "selectVariant"),
        ):
            start = body.index("SetupResults::" + case)
            end = body.index("case Instance::SetupResults::", start + 10) if (
                "case Instance::SetupResults::" in body[start + 10 :]
            ) else len(body)
            section = body[start:end]
            self._assert_before(section, "headless::active()", picker + "(")
            guard = function_body(section, r"if \(headless::active\(\)\)")
            self.assertIn("SetupInstanceResults::Exit", guard)

    def test_start_binary_uses_report_launch_problem(self) -> None:
        source = self._read("spawn.cpp")
        body = function_body(source, r"env::NativeProcess startBinary\(QWidget\* parent")
        self.assertNotIn("QMessageBox::critical(", body)
        self.assertGreaterEqual(body.count("reportLaunchProblem("), 4)
        helper = function_body(source, r"void reportLaunchProblem\(")
        self.assertIn("headless::fail(text)", helper)
        self.assertIn("QMessageBox::critical(parent, title, text)", helper)
        anon = re.search(r"\nnamespace\s*\{", source)
        self.assertIsNotNone(anon, "no anonymous namespace in spawn.cpp")
        self.assertLess(anon.start(), source.index("void reportLaunchProblem("))
        self.assertNotIn("reportLaunchProblem", self._read("spawn.h"))

    def test_check_steam_continues_headless(self) -> None:
        body = self._body_of("spawn.cpp", r"bool checkSteam\(QWidget\* parent")
        self.assertIn(
            "headless: Steam isn't running; continuing without starting it", body
        )
        self._assert_before(body, "headless::active()", "confirmStartSteam(")
        guard = function_body(body, r"if \(headless::active\(\)\)")
        self.assertIn("return true;", guard)

    def test_registry_prompt_auto_answers_yes_headless(self) -> None:
        body = self._body_of("organizercore.cpp", r"bool OrganizerCore::checkGameRegistryKey\(\)")
        self.assertIn("headless: registry key updated from '{}' to '{}'", body)
        self._assert_before(body, "headless::active()", "QMessageBox::question(")
        self.assertRegex(body, r"headless::active\(\)\s*\?\s*QMessageBox::Yes\s*:\s*QMessageBox::question\(")

    def test_before_run_catch_routes_headless(self) -> None:
        body = self._body_of("organizercore.cpp", r"bool OrganizerCore::beforeRun\(")
        catch = body[body.index("catch (const std::exception& e)") :]
        self._assert_before(catch, "headless::fail(", "QMessageBox::warning(")
        self.assertIn("headless::active()", catch[: catch.index("QMessageBox::warning(")])

    def test_organizercore_critical_sites_headless(self) -> None:
        source = self._read("organizercore.cpp")
        write = function_body(source, r"void OrganizerCore::saveCurrentLists\(\)|void OrganizerCore::storeSettings\(\)")
        self._assert_before(write, "headless::fail(", "QMessageBox::critical(")
        create = function_body(source, r"bool OrganizerCore::createDirectory\(const QString& path\)")
        self._assert_before(create, "headless::fail(", "QMessageBox::critical(")
        self.assertIn("headless::active()", create)

    def test_fuse_catalog_dialog_suppressed_headless(self) -> None:
        source = self._read("fuseconnector.cpp")
        self.assertRegex(
            source,
            r"if \(qApp != nullptr && !headless::active\(\) &&\s*QThread::currentThread\(\) == qApp->thread\(\)\)\s*\{\s*catalogProgress = std::make_unique<QProgressDialog>",
        )

    def test_profile_ini_warning_skipped_headless(self) -> None:
        body = self._body_of("profile.cpp", r"bool Profile::localSettingsEnabled\(\) const")
        self.assertIn("headless: profile-specific INI files missing; continuing", body)
        self._assert_before(body, "headless::active()", "QMessageBox::warning(")
        self.assertIn("return enabled;", body)

    def test_plugin_loadcheck_skips_headless(self) -> None:
        source = self._read("plugincontainer.cpp")
        start = source.index("MOBase::TaskDialog dlg;")
        section = source[start : source.index("switch (r)", start)]
        self._assert_before(section, "headless::active()", ".exec()")
        self.assertRegex(section, r"headless::active\(\)\s*\?\s*Skip\s*:")
        self.assertNotRegex(section, r"headless::active\(\)\s*\?\s*Blacklist")

    def test_headless_setup_keeps_last_instance(self) -> None:
        source = self._read("moapplication.cpp")
        loop = function_body(source, r"MOApplication::setupInstanceLoop\(")
        select = loop[loop.index("SelectAnother") :]
        self._assert_before(select, "headless::active()", "clearCurrentInstance()")
        guard = function_body(select, r"if \(headless::active\(\)\)")
        self.assertIn("return 1;", guard)
        setup = function_body(source, r"int MOApplication::setup\(")
        for call in re.finditer(r"clearCurrentInstance\(\)", setup):
            before = setup[: call.start()]
            self.assertRegex(before[-120:], r"!headless::active\(\)")

    def test_java_picker_skipped_headless(self) -> None:
        body = self._body_of("spawn.cpp", r"FileExecutionContext getFileExecutionContext\(")
        java = body[body.index("isJavaFile(target)") :]
        self._assert_before(java, "headless::active()", "QFileDialog::getOpenFileName(")
        guard = function_body(java, r"if \(headless::active\(\)\)")
        self.assertIn("FileExecutionTypes::Other", guard)


if __name__ == "__main__":
    unittest.main()
