"""Host checks of the native export contract; not a native build or rc run."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class IntegrationTests(unittest.TestCase):
    def manifest(self, name):
        return (ROOT / "integration" / name).read_text().splitlines()

    def config(self, text):
        with tempfile.TemporaryDirectory(prefix="vconsole-config-") as directory:
            path = Path(directory) / "pc64"
            path.write_text(text)
            return subprocess.run(
                ["awk", "-v", "errfile=/dev/stderr", "-f", str(ROOT / "integration/config.awk"),
                 str(ROOT / "integration/port.modules"), str(ROOT / "integration/pc.modules"), str(path)],
                capture_output=True, text=True, check=False)

    def test_manifests_cover_sources_once(self):
        modules = self.manifest("port.modules") + self.manifest("pc.modules")
        self.assertEqual(len(modules), 17)
        self.assertEqual(len(modules), len(set(modules)))
        self.assertEqual({name + ".c" for name in modules},
                         {path.name for path in (ROOT / "kernel").glob("*.c")})
        headers = self.manifest("port.headers")
        self.assertEqual(len(headers), 16)
        self.assertEqual(len(headers), len(set(headers)))
        self.assertEqual(set(headers), {path.name for path in (ROOT / "kernel").glob("*.h")})

    def test_build_rules_match_manifest_and_track_headers(self):
        rules = (ROOT / "integration/vconsole.mk").read_text()
        for variable, name in [("VCPORTMODULES", "port.modules"), ("VCPCMODULES", "pc.modules")]:
            value = next(line.split("=", 1)[1] for line in rules.splitlines() if line.startswith(variable + "="))
            self.assertEqual(value.split("|"), self.manifest(name))
        for name in self.manifest("port.headers"):
            self.assertIn("$VCPORT/" + name, rules)
        self.assertEqual(rules.count("$VCHFILES ../port/virtio10.h"), 2)

    def test_configuration_uses_native_dev_and_misc_entries(self):
        stock = "# fixture\ndev\n\troot\n\tcons\nlink\n\tether\nmisc\n\tpci\nport\n\tint cpuserver = 0;\n"
        result = self.config(stock)
        self.assertEqual(result.returncode, 0, result.stderr)
        modules = self.manifest("port.modules") + self.manifest("pc.modules")
        extra = "".join("\t" + name + "\n" for name in modules if name != "devvconsole")
        expected = stock.replace("dev\n", "dev\n\tvconsole\n").replace("misc\n", "misc\n" + extra)
        self.assertEqual(result.stdout, expected)

    def test_native_object_list_preserves_hyphenated_modules(self):
        # Native mkdevlist treats [+=-] in dependency fields as flags. Keep our
        # names as first misc fields; a long dev dependency list would drop them.
        result = self.config("dev\n\troot\nmisc\n\tpci\nport\n\tint cpuserver = 0;\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        section, objects = "", set()
        for line in result.stdout.splitlines():
            if line and not line[0].isspace():
                section = line
            elif line.strip() and section in ["dev", "misc"]:
                fields = line.split()
                objects.add(("dev" if section == "dev" else "") + fields[0])
                objects.update(name for name in fields[1:] if not re.search(r"[+=-].*", name))
        self.assertTrue(set(self.manifest("port.modules") + self.manifest("pc.modules")) <= objects)

    def test_bad_or_duplicate_configuration_has_no_output(self):
        for stock in ["misc\n\tpci\n", "dev\n", "dev\ndev\nmisc\n", "dev\nmisc\nmisc\n",
                      "dev\n\tvconsole\nmisc\n", "dev\nmisc\n\tvconsole-pc\n"]:
            with self.subTest(stock=stock):
                result = self.config(stock)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertIn("expected one dev/misc", result.stderr)

    def test_production_has_no_absolute_architecture_includes(self):
        for path in (ROOT / "kernel").glob("*.c"):
            self.assertNotIn('"/sys/src/9/', path.read_text(), str(path))
        helper = (ROOT / "kernel/build-pc64.rc").read_text()
        self.assertNotIn("-kernel.c", helper)
        self.assertNotIn("qga9kern", helper)
        self.assertIn("scripts/export-upstream.rc", helper)
        self.assertIn("test -e $vc_conf", helper)
        for variable in ["CONF", "VCSOURCE", "VCPORT", "VCPC"]:
            self.assertIn("'" + variable + "='^", helper)

    def test_export_preserves_production_sources_and_manual(self):
        script = (ROOT / "scripts/export-upstream.rc").read_text()
        for name in ["port.modules", "pc.modules", "port.headers"]:
            self.assertIn("integration/" + name, script)
        self.assertIn("test -e $vc_export", script)
        self.assertIn("cp man/3/vconsole", script)
        self.assertTrue((ROOT / "man/3/vconsole").read_text().startswith(".TH VCONSOLE 3\n"))
        mkfile = (ROOT / "mkfile").read_text()
        first_target = next(line for line in mkfile.splitlines() if ":" in line and not line.startswith("#"))
        self.assertTrue(first_target.startswith("all:V:"))

    def test_upstream_patch_applies_and_matches_dependencies(self):
        # Exact small contexts from the inspected native pc64 configuration/rules.
        stock = "# pc64 - amd64 pc terminal with local disk\ndev\n\troot\n\tcons\n\tswap\n"
        stock += "misc\n\tpci\t\tpcipc\n\tarchgeneric\tdevkbd i8259 i8253\n\tarchacpi\tmp apic squidboy ec hpet\n"
        rules = ("\nirq.$O pcipc.$O:\t../port/pci.h io.h\n"
                 "virtio10pc.$O:\t../port/virtio10.h\n"
                 "initcode.out:\t\tinit9.$O initcode.$O /$objtype/lib/libc.a\n"
                 "\t$LD -l -R1 -s -o $target $prereq\n\n")
        with tempfile.TemporaryDirectory(prefix="vconsole-patch-") as directory:
            target = Path(directory) / "sys/src/9/pc64"
            target.mkdir(parents=True)
            (target / "pc64").write_text(stock)
            (target / "mkfile").write_text(rules)
            result = subprocess.run(["git", "apply", str(ROOT / "integration/pc64.patch")],
                                    cwd=directory, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((target / "pc64").read_text(), self.config(stock).stdout)
            self.assertEqual((target / "mkfile").read_text(), rules.replace(
                "initcode.out:", "<vconsole.mk\n\ninitcode.out:"))


if __name__ == "__main__":
    unittest.main()
