"""End-to-end tests for the executable; no third-party dependencies."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = os.environ.get('BASIC_BINARY', str(ROOT / 'gkbasic'))


def invoke(source, *args):
    return subprocess.run([BINARY, '-q', *args], input=source, text=True,
                          capture_output=True, timeout=5)


class BasicTests(unittest.TestCase):
    def check(self, source, expected):
        result = invoke(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, expected)

    def test_arithmetic(self):
        self.check('PRINT 2+3*4;",";2^3^2;",";-2^2;",";7 MOD 3\n'
                   'PRINT NOT 0;",";3>2;",";INT(-1.2)\n',
                   '14,512,-4,1\n-1,-1,-2\n')

    def test_strings(self):
        self.check('A$="HELLO":PRINT LEFT$(A$,2)+RIGHT$(A$,2);MID$(A$,2,3)\n'
                   'PRINT LEN(A$);ASC("A");CHR$(66)\n', 'HELOELL\n565B\n')

    def test_edit_list_delete(self):
        self.check('30 END\n10 PRINT "OLD"\n20 PRINT "DELETE"\n'
                   '10 PRINT "NEW"\n20\nLIST\nRUN\n',
                   '10 PRINT "NEW"\n30 END\nNEW\n')

    def test_for_gosub(self):
        self.check('10 FOR I=1 TO 3:GOSUB 100:NEXT I\n20 END\n'
                   '100 PRINT I;:RETURN\nRUN\n', '123')

    def test_nested_descending_loops(self):
        self.check('10 FOR I=2 TO 1 STEP -1\n20 FOR J=1 TO 2\n'
                   '30 PRINT I;J;","\n40 NEXT J\n50 NEXT I\nRUN\n',
                   '21,\n22,\n11,\n12,\n')

    def test_empty_loop(self):
        self.check('10 FOR I=5 TO 1\n20 FOR J=1 TO 2\n30 PRINT "BAD"\n'
                   '40 NEXT J\n50 NEXT I:PRINT "OK"\nRUN\n', 'OK\n')

    def test_if(self):
        self.check('10 IF 0 THEN PRINT "BAD" ELSE PRINT "GOOD"\n'
                   '20 IF 1 THEN PRINT "YES":PRINT "ALSO" ELSE PRINT "BAD"\n'
                   '30 IF 0 THEN PRINT "BAD":PRINT "BAD"\n'
                   '40 IF 1 THEN 60\n50 PRINT "BAD"\n60 END\nRUN\n',
                   'GOOD\nYES\nALSO\n')

    def test_data_restore(self):
        self.check('10 DATA 42,"HELLO":DATA -3\n'
                   '20 READ A,B$,C:PRINT A;B$;C\n'
                   '30 RESTORE:READ A:PRINT A\nRUN\n', '42HELLO-3\n42\n')

    def test_arrays(self):
        self.check('10 DIM A(2,3),N$(2)\n20 A(2,3)=42:N$(1)="ARRAY"\n'
                   '30 PRINT A(2,3);N$(1);A(0,0)\n'
                   '40 B(10)=7:PRINT B(10)\nRUN\n', '42ARRAY0\n7\n')
        result = invoke('DIM A(2)\nPRINT A(3)\n')
        self.assertIn('Subscript out of range', result.stderr)

    def test_direct_if_and_quoted_else(self):
        self.check('IF 1 THEN PRINT "YES" ELSE PRINT "NO"\n'
                   'IF 0 THEN PRINT "ELSE" ELSE PRINT "OK"\n', 'YES\nOK\n')

    def test_comment_data(self):
        self.check('10 REM ignore: DATA 99\n20 DATA 7\n'
                   '30 READ A:PRINT A\nRUN\n', '7\n')

    def test_input(self):
        self.check('10 INPUT "Name";N$\n20 INPUT A\n'
                   '30 PRINT N$;A*2\nRUN\nAda\n21\n', 'Name? ? Ada42\n')

    def test_recovery(self):
        result = invoke('10 PRINT 1/0\nRUN\nPRINT "RECOVERED"\n')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Division by zero in 10', result.stderr)
        self.assertEqual(result.stdout, 'RECOVERED\n')

    def test_deep_expression_recovery(self):
        result = invoke('PRINT ' + '(' * 100 + '1' + ')' * 100 + '\nPRINT 2\n')
        self.assertIn('Expression too complex', result.stderr)
        self.assertEqual(result.stdout, '2\n')

    def test_conditional_subroutine(self):
        self.check('10 IF 1 THEN GOSUB 100 ELSE PRINT "BAD"\n'
                   '20 END\n100 PRINT "OK":RETURN\nRUN\n', 'OK\n')

    def test_runtime_errors(self):
        for statement, message in [('RETURN', 'RETURN without GOSUB'),
                                   ('NEXT', 'NEXT without FOR'),
                                   ('GOTO 999', 'Undefined line'),
                                   ('A$=3', 'Type mismatch'),
                                   ('PRINT SQR(-1)', 'Illegal function'),
                                   ('READ A', 'Out of DATA')]:
            with self.subTest(statement=statement):
                result = invoke(f'10 {statement}\nRUN\n')
                self.assertEqual(result.returncode, 1)
                self.assertIn(message, result.stderr)

    def test_save_load_batch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'hello.bas'
            self.check(f'10 PRINT "SAVED"\nSAVE "{path}"\nNEW\n'
                       f'LOAD "{path}"\nRUN\n', 'SAVED\n')
            result = invoke('', str(path))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, 'SAVED\n')

    def test_bad_load_preserves_program(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.bas'
            path.write_text('not a BASIC program\n')
            result = invoke(f'10 PRINT "KEPT"\nLOAD "{path}"\nRUN\n')
            self.assertEqual(result.stdout, 'KEPT\n')
            self.assertIn('Invalid BASIC file', result.stderr)


if __name__ == '__main__':
    unittest.main()
