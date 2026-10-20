"""Bounded Windows process-tree ownership for trusted local test/build tools.

Each child starts suspended, enters an unnamed kill-on-close job, then resumes.
Success requires both parent exit and an empty job; logs are regular files, so
pipe EOF cannot masquerade as descendant cleanup. This is not a hostile process
sandbox and confers no product, C15, producer-enrollment or G2 authority.
"""
from __future__ import annotations

import ctypes as c
from ctypes import wintypes as w
import datetime
import hashlib
from pathlib import Path
import locale
import os
import shutil
import subprocess
import tempfile
import threading
import time

_SOURCE_PATH = str(Path(__file__).resolve())
_SOURCE_SHA256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()

STDOUT = subprocess.STDOUT
_launch_lock = threading.Lock()
_unresolved_cleanup = False


class SECURITY_ATTRIBUTES(c.Structure):
    _fields_ = [('length', w.DWORD), ('descriptor', w.LPVOID), ('inherit', w.BOOL)]


class STARTUPINFO(c.Structure):
    _fields_ = [('cb', w.DWORD), ('reserved', w.LPWSTR), ('desktop', w.LPWSTR), ('title', w.LPWSTR),
        ('x', w.DWORD), ('y', w.DWORD), ('xsize', w.DWORD), ('ysize', w.DWORD), ('xchars', w.DWORD),
        ('ychars', w.DWORD), ('fill', w.DWORD), ('flags', w.DWORD), ('show', w.WORD),
        ('reserved_count', w.WORD), ('reserved_bytes', w.LPVOID),
        ('stdin', w.HANDLE), ('stdout', w.HANDLE), ('stderr', w.HANDLE)]


class STARTUPINFOEX(c.Structure):
    _fields_ = [('info', STARTUPINFO), ('attributes', w.LPVOID)]


class PROCESS_INFORMATION(c.Structure):
    _fields_ = [('process', w.HANDLE), ('thread', w.HANDLE), ('pid', w.DWORD), ('tid', w.DWORD)]


class BASIC_LIMITS(c.Structure):
    _fields_ = [('process_time', c.c_int64), ('job_time', c.c_int64), ('flags', w.DWORD),
        ('minimum_working_set', c.c_size_t), ('maximum_working_set', c.c_size_t),
        ('active_limit', w.DWORD), ('affinity', c.c_size_t), ('priority', w.DWORD), ('scheduling', w.DWORD)]


class IO_COUNTERS(c.Structure):
    _fields_ = [(name, c.c_uint64) for name in ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]


class EXTENDED_LIMITS(c.Structure):
    _fields_ = [('basic', BASIC_LIMITS), ('io', IO_COUNTERS), ('process_memory', c.c_size_t),
        ('job_memory', c.c_size_t), ('peak_process_memory', c.c_size_t), ('peak_job_memory', c.c_size_t)]


class ACCOUNTING(c.Structure):
    _fields_ = [('user', c.c_int64), ('kernel', c.c_int64), ('period_user', c.c_int64), ('period_kernel', c.c_int64),
        ('faults', w.DWORD), ('total_processes', w.DWORD), ('active_processes', w.DWORD), ('terminated_processes', w.DWORD)]


def _api():
    if os.name != 'nt' or c.sizeof(c.c_void_p) != 8:
        raise OSError('Windows x64 process supervision is required')
    api = c.WinDLL('kernel32', use_last_error=True)
    signatures = {
        'CreateJobObjectW': (w.HANDLE, [w.LPVOID, w.LPCWSTR]),
        'SetInformationJobObject': (w.BOOL, [w.HANDLE, c.c_int, w.LPVOID, w.DWORD]),
        'QueryInformationJobObject': (w.BOOL, [w.HANDLE, c.c_int, w.LPVOID, w.DWORD, w.LPVOID]),
        'AssignProcessToJobObject': (w.BOOL, [w.HANDLE, w.HANDLE]),
        'TerminateJobObject': (w.BOOL, [w.HANDLE, w.UINT]),
        'TerminateProcess': (w.BOOL, [w.HANDLE, w.UINT]),
        'GetCurrentProcess': (w.HANDLE, []),
        'DuplicateHandle': (w.BOOL, [w.HANDLE, w.HANDLE, w.HANDLE, c.POINTER(w.HANDLE), w.DWORD, w.BOOL, w.DWORD]),
        'CloseHandle': (w.BOOL, [w.HANDLE]),
        'FlushFileBuffers': (w.BOOL, [w.HANDLE]),
        'InitializeProcThreadAttributeList': (w.BOOL, [w.LPVOID, w.DWORD, w.DWORD, c.POINTER(c.c_size_t)]),
        'UpdateProcThreadAttribute': (w.BOOL, [w.LPVOID, w.DWORD, c.c_size_t, w.LPVOID, c.c_size_t, w.LPVOID, w.LPVOID]),
        'DeleteProcThreadAttributeList': (None, [w.LPVOID]),
        'CreateProcessW': (w.BOOL, [w.LPCWSTR, w.LPWSTR, w.LPVOID, w.LPVOID, w.BOOL, w.DWORD,
            w.LPVOID, w.LPCWSTR, w.LPVOID, c.POINTER(PROCESS_INFORMATION)]),
        'ResumeThread': (w.DWORD, [w.HANDLE]),
        'WaitForSingleObject': (w.DWORD, [w.HANDLE, w.DWORD]),
        'GetExitCodeProcess': (w.BOOL, [w.HANDLE, c.POINTER(w.DWORD)]),
    }
    for name, (result, arguments) in signatures.items():
        method = getattr(api, name); method.restype = result; method.argtypes = arguments
    return api


def _need(ok, operation):
    if not ok:
        raise OSError(c.get_last_error(), operation)


def _now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def developer_environment_script(vcvars):
    """Export only compiler environment fields, never the whole inherited env."""
    value = str(vcvars)
    if any(character in value for character in '"%!\r\n&|<>^'):
        raise ValueError('unsupported vcvars launcher path')
    names = 'PATH INCLUDE LIB LIBPATH VCToolsVersion VCToolsInstallDir VSCMD_ARG_TGT_ARCH VSCMD_ARG_HOST_ARCH VisualStudioVersion WindowsSDKVersion WindowsSdkDir UCRTVersion UniversalCRTSdkDir'
    return '@echo off\ncall "'+value+'" x64 -vcvars_ver=14.44.35207 >nul\nif errorlevel 1 exit /b 1\nfor %%V in ('+names+') do set %%V\nexit /b 0\n'


def run(command, *, cwd=None, env=None, timeout, capture_output=False, text=False,
        stdout=None, stderr=None, encoding=None, errors='replace'):
    """Return CompletedProcess plus supervision and infrastructure_error fields.

    Infrastructure failure returns 125, timeout 124; normal numeric child exits
    remain unchanged. Partial captures and cleanup facts are always returned.
    An unresolved cleanup poisons this helper instance: later launches refuse.
    Only argv lists and regular file output handles are supported.
    """
    global _unresolved_cleanup
    args = list(map(str, command))
    observation = dict(supervisor_source_path=_SOURCE_PATH, supervisor_source_sha256=_SOURCE_SHA256, process_created=False, user_code_started=False, process_id=None,
        group_assigned_before_resume=False, parent_exit_observed=False, group_empty_observed=False,
        last_active_processes=None, numeric_exit_code=None, timed_out=False, termination_requested=False,
        capture_complete=False, timeout_observation=None, errors=[], cleanup_errors=[],
        started_utc=_now(), ended_utc=None, command=args, cwd=str(cwd) if cwd is not None else os.getcwd(), timeout_seconds=timeout)
    api = None; job = None; process = PROCESS_INFORMATION(); attributes = None; attribute_ready = False
    duplicates = []; files = []; buffers = []; assigned = False; own_stdout = own_stderr = False
    out_file = err_file = None
    code = 125
    try:
        if not args or isinstance(command, (str, bytes)) or not (0 < timeout <= 7200):
            raise ValueError('nonempty argv list and timeout in (0,7200] required')
        if capture_output and (stdout is not None or stderr is not None):
            raise ValueError('capture_output conflicts with explicit output handles')
        api = _api()
        import msvcrt
        if stdout is None:
            out_file = tempfile.TemporaryFile('w+b'); files.append(out_file); own_stdout = True
        else:
            out_file = stdout
        if stderr == STDOUT:
            err_file = out_file
        elif stderr is None:
            err_file = tempfile.TemporaryFile('w+b'); files.append(err_file); own_stderr = True
        else:
            err_file = stderr
        input_file = open(os.devnull, 'rb'); files.append(input_file)
        parent = api.GetCurrentProcess()
        for stream in (input_file, out_file, err_file):
            stream.flush()
            copied = w.HANDLE()
            _need(api.DuplicateHandle(parent, msvcrt.get_osfhandle(stream.fileno()), parent, c.byref(copied), 0, True, 2), 'Duplicate child stdio handle')
            duplicates.append(copied.value)
        job = api.CreateJobObjectW(None, None); _need(job, 'CreateJobObject')
        limits = EXTENDED_LIMITS(); limits.basic.flags = 0x2000
        _need(api.SetInformationJobObject(job, 9, c.byref(limits), c.sizeof(limits)), 'Set kill-on-close job limits')
        size = c.c_size_t()
        api.InitializeProcThreadAttributeList(None, 1, 0, c.byref(size))
        if not 0 < size.value <= 65536:
            raise OSError('invalid process attribute allocation size')
        attributes = c.create_string_buffer(size.value)
        _need(api.InitializeProcThreadAttributeList(attributes, 1, 0, c.byref(size)), 'Initialize process attributes')
        attribute_ready = True
        inherited = (w.HANDLE * 3)(*duplicates)
        _need(api.UpdateProcThreadAttribute(attributes, 0, 0x20002, inherited, c.sizeof(inherited), None, None), 'Restrict inherited handle list')
        start = STARTUPINFOEX(); start.info.cb = c.sizeof(start); start.info.flags = 0x100
        start.info.stdin, start.info.stdout, start.info.stderr = duplicates
        start.attributes = c.cast(attributes, w.LPVOID)
        environment = dict(os.environ if env is None else env)
        if any('\0' in str(key) or '\0' in str(value) for key, value in environment.items()):
            raise ValueError('environment contains a null character')
        environment_buffer = c.create_unicode_buffer('\0'.join(str(key)+'='+str(value) for key, value in sorted(environment.items(), key=lambda item: item[0].upper()))+'\0\0')
        buffers.append(environment_buffer)
        executable = shutil.which(args[0], path=environment.get('PATH', environment.get('Path')))
        if executable is None:
            raise FileNotFoundError(args[0])
        command_line = subprocess.list2cmdline(args)
        observation['executable'] = executable
        observation['windows_argument_string'] = command_line
        if len(command_line) >= 32767:
            raise ValueError('Windows command line limit')
        command_buffer = c.create_unicode_buffer(command_line)
        with _launch_lock:
            if _unresolved_cleanup:
                raise OSError('earlier command cleanup unresolved; further launches refused')
            _need(api.CreateProcessW(executable, command_buffer, None, None, True,
                0x08000000 | 0x00080000 | 0x00000400 | 0x00000004,
                environment_buffer, str(cwd) if cwd is not None else None, c.byref(start), c.byref(process)), 'Create suspended process')
            observation['process_created'] = True; observation['process_id'] = process.pid
            _need(api.AssignProcessToJobObject(job, process.process), 'Assign suspended process to owned job')
            assigned = True; observation['group_assigned_before_resume'] = True
            _need(api.ResumeThread(process.thread) != 0xffffffff, 'Resume owned process')
            observation['user_code_started'] = True
        deadline = time.monotonic() + timeout

        def poll():
            if not observation['parent_exit_observed']:
                waited = api.WaitForSingleObject(process.process, 0)
                _need(waited != 0xffffffff, 'Observe parent exit')
                if waited == 0:
                    value = w.DWORD(); _need(api.GetExitCodeProcess(process.process, c.byref(value)), 'Read parent exit')
                    observation['numeric_exit_code'] = value.value; observation['parent_exit_observed'] = True
            accounting = ACCOUNTING()
            _need(api.QueryInformationJobObject(job, 1, c.byref(accounting), c.sizeof(accounting), None), 'Observe owned job membership')
            observation['last_active_processes'] = accounting.active_processes
            observation['group_empty_observed'] = observation['parent_exit_observed'] and accounting.active_processes == 0
            return observation['group_empty_observed']

        while not poll():
            if time.monotonic() >= deadline:
                observation['timed_out'] = True
                observation['timeout_observation'] = dict(parent_exit_observed=observation['parent_exit_observed'], active_processes=observation['last_active_processes'])
                raise TimeoutError('owned process group exceeded deadline')
            time.sleep(.02)
        code = observation['numeric_exit_code']
    except (OSError, ValueError, TypeError) as error:
        observation['errors'].append(repr(error))
    finally:
        if observation['process_created'] and not observation['group_empty_observed']:
            observation['termination_requested'] = True
            try:
                _need(api.TerminateJobObject(job, 1) if assigned else api.TerminateProcess(process.process, 1), 'Terminate exact owned process group')
                cleanup_deadline = time.monotonic() + 5
                while time.monotonic() < cleanup_deadline:
                    waited = api.WaitForSingleObject(process.process, 0)
                    _need(waited != 0xffffffff, 'Observe terminated parent')
                    if waited == 0:
                        value = w.DWORD(); _need(api.GetExitCodeProcess(process.process, c.byref(value)), 'Read terminated parent exit')
                        observation['numeric_exit_code'] = value.value; observation['parent_exit_observed'] = True
                    accounting = ACCOUNTING()
                    _need(api.QueryInformationJobObject(job, 1, c.byref(accounting), c.sizeof(accounting), None), 'Observe terminated job empty')
                    observation['last_active_processes'] = accounting.active_processes
                    if observation['parent_exit_observed'] and accounting.active_processes == 0:
                        observation['group_empty_observed'] = True; break
                    time.sleep(.02)
                if not observation['group_empty_observed']:
                    raise OSError('owned process group cleanup remains unresolved')
            except (OSError, ValueError) as error:
                observation['cleanup_errors'].append(repr(error))
            if not observation['group_empty_observed']:
                with _launch_lock:
                    _unresolved_cleanup = True
        if observation['group_empty_observed']:
            try:
                for handle in duplicates[1:]:
                    _need(api.FlushFileBuffers(handle), 'Flush completed capture')
                observation['capture_complete'] = True
            except OSError as error:
                observation['cleanup_errors'].append(repr(error))
        if attribute_ready:
            api.DeleteProcThreadAttributeList(attributes)
        if api is not None:
            for handle in (process.thread, process.process, job, *duplicates):
                if handle and not api.CloseHandle(handle):
                    observation['cleanup_errors'].append(repr(OSError(c.get_last_error(), 'Close owned native handle')))
                    observation['capture_complete'] = False
        captured = []
        for stream, owned in ((out_file, own_stdout), (err_file, own_stderr)):
            data = None
            if stream is not None and owned and capture_output:
                try:
                    stream.flush(); stream.seek(0); data = stream.read()
                    if text:
                        data = data.decode(encoding or locale.getpreferredencoding(False), errors=errors)
                        data = data.replace('\r\n', '\n').replace('\r', '\n')
                except (OSError, ValueError) as error:
                    observation['cleanup_errors'].append(repr(error)); observation['capture_complete'] = False
            captured.append(data)
        for stream in files:
            stream.close()
        observation['ended_utc'] = _now()
    infrastructure = bool(observation['errors'] or observation['cleanup_errors'] or not observation['group_empty_observed'] or not observation['capture_complete'])
    if infrastructure:
        code = 124 if observation['timed_out'] else 125
    result = subprocess.CompletedProcess(args, code, *captured)
    result.supervision = observation; result.infrastructure_error = infrastructure
    return result
