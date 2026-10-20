"""Uploader validation and credential handling, never a live flash."""
import importlib.util
from pathlib import Path
import tempfile
import os
import unittest
from unittest import mock
import types
import contextlib
import io
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('upload_ota',ROOT/'tools/upload_ota.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class UploaderTests(unittest.TestCase):
    def test_dry_run_loads_real_platformio_sender_without_prompt_or_network(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            firmware=Path(tmp)/'firmware.bin'; firmware.write_bytes(b'\xe9'+bytes(32))
            transport=types.SimpleNamespace(serve=mock.Mock(),FLASH=0)
            with mock.patch.object(module,'load_sender',return_value=transport), mock.patch.object(module.getpass,'getpass') as prompt, contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertEqual(module.main(['--ip','192.0.2.1','--firmware',str(firmware),'--dry-run']),0)
                prompt.assert_not_called(); transport.serve.assert_not_called()
                self.assertIn('192.0.2.1',out.getvalue())
    def test_password_passed_in_process_without_becoming_command_argument_or_log(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            firmware=Path(tmp)/'firmware.bin'; firmware.write_bytes(b'\xe9'+bytes(32))
            transport=types.SimpleNamespace(serve=mock.Mock(return_value=0),FLASH=0)
            password='public-fixture-password'
            with mock.patch.object(module,'load_sender',return_value=transport), mock.patch.object(module.getpass,'getpass',return_value=password), contextlib.redirect_stdout(io.StringIO()) as out, contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(module.main(['--ip','192.0.2.1','--firmware',str(firmware)]),0)
                self.assertNotIn(password,out.getvalue()+err.getvalue())
                args=transport.serve.call_args.args
                self.assertEqual(args,('192.0.2.1','0.0.0.0',3232,32323,password,str(firmware.resolve()),0))
                self.assertEqual(transport.TIMEOUT,10)
    def test_bad_image_and_nonliteral_ip_fail_before_password_or_sender(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            firmware=Path(tmp)/'filesystem.bin'; firmware.write_bytes(bytes(33))
            with mock.patch.object(module,'load_sender') as load, mock.patch.object(module.getpass,'getpass') as prompt, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module.main(['--ip','192.0.2.1','--firmware',str(firmware)]),2)
                self.assertEqual(module.main(['--ip','192.0.2.999','--firmware',str(firmware)]),2)
                prompt.assert_not_called(); load.assert_not_called()
    def test_no_echo_control_aborts_before_transfer(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            firmware=Path(tmp)/'firmware.bin'; firmware.write_bytes(b'\xe9'+bytes(32))
            transport=types.SimpleNamespace(serve=mock.Mock(),FLASH=0)
            with mock.patch.object(module,'load_sender',return_value=transport), mock.patch.object(module.getpass,'getpass',side_effect=module.getpass.GetPassWarning('Cannot control echo')), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module.main(['--ip','192.0.2.1','--firmware',str(firmware)]),2)
                transport.serve.assert_not_called()
    def test_oversized_image_and_missing_sender_fail_safely(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            firmware=Path(tmp)/'firmware.bin'
            with firmware.open('wb') as file:
                file.write(b'\xe9'); file.truncate(0x330001)
            with mock.patch.object(module.getpass,'getpass') as prompt, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module.main(['--ip','192.0.2.1','--firmware',str(firmware),'--dry-run']),2)
                prompt.assert_not_called()
            firmware.write_bytes(b'\xe9'+bytes(32))
            with mock.patch.object(module,'load_sender',side_effect=FileNotFoundError('sender absent')), mock.patch.object(module.getpass,'getpass') as prompt, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module.main(['--ip','192.0.2.1','--firmware',str(firmware)]),2)
                prompt.assert_not_called()
if __name__=='__main__': unittest.main()
