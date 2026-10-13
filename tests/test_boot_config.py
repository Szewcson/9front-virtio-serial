# SPDX-License-Identifier: MIT
import pathlib
import subprocess
import unittest

PROGRAM = pathlib.Path(__file__).resolve().parents[1] / 'kernel' / 'boot-config.awk'
STOCK = 'bootfile=9pc64\nbootargs=local!/dev/sd00/fs -m 296\nmouseport=ps2\n\nservice=cpu\n'


class BootConfigTests(unittest.TestCase):
    def convert(self, source):
        return subprocess.run(['awk', '-f', str(PROGRAM)], input=source,
                              text=True, capture_output=True, check=False)

    def reject(self, source):
        result = self.convert(source)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')

    def test_preserves_unrelated_options_and_exact_local_args(self):
        result = self.convert(STOCK)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, STOCK.replace('bootfile=9pc64\n', '')
                         + 'bootfile=9pc64qga\nqgavirtio=1\n'
                         + 'nobootprompt=local!/dev/sd00/fs -m 296\n')

    def test_replaces_old_enable_and_prompt_knobs_once(self):
        result = self.convert(STOCK + 'qgavirtio=0\nnobootprompt=broken\nqgavirtio=0\n')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.count('qgavirtio='), 1)
        self.assertEqual(result.stdout.count('nobootprompt='), 1)

    def test_missing_kernel(self):
        self.reject(STOCK.replace('bootfile=9pc64\n', ''))

    def test_duplicate_kernel(self):
        self.reject(STOCK + 'bootfile=9pc64\n')

    def test_unknown_kernel(self):
        self.reject(STOCK.replace('bootfile=9pc64', 'bootfile=custom'))

    def test_menu_config(self):
        self.reject('[menu]\n' + STOCK)

    def test_missing_args(self):
        self.reject('bootfile=9pc64\n')

    def test_duplicate_args(self):
        self.reject(STOCK + 'bootargs=local!other\n')

    def test_non_local_or_empty_args(self):
        for args in ('', 'tcp!remote', '1', 'local'):
            with self.subTest(args=args):
                self.reject('bootfile=9pc64\nbootargs=' + args + '\n')

    def test_input_line_bound(self):
        self.reject(STOCK + 'extra=' + 'x' * 4096 + '\n')

    def test_input_record_bound(self):
        self.reject(STOCK + '\n' * 48)

    def test_total_output_bound(self):
        self.reject(STOCK + 'long=' + 'x' * 2000 + '\n')

    def test_bootargs_bound(self):
        self.reject('bootfile=9pc64\nbootargs=local!' + 'x' * 1024 + '\n')


if __name__ == '__main__':
    unittest.main()
