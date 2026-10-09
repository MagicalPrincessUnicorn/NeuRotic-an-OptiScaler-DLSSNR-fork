"""Explicit package defaults; never edits an installed game's saved INI."""
import re


def _defaults(profile):
    if profile not in ('public', 'internal-testing'):
        raise ValueError('Unknown logging profile: ' + str(profile))
    return {'LogToFile': 'true' if profile == 'internal-testing' else 'false',
            'LogLevel': '0' if profile == 'internal-testing' else '2',
            'LogToConsole': 'false', 'LogToNGX': 'false',
            'LogToDebug': 'false', 'OpenConsole': 'false'}


def _section(ini):
    headers = list(re.finditer(rb'(?m)^[ \t]*\[([^\]\r\n]+)\][^\r\n]*(?:\r?\n|$)', ini))
    logs = [i for i, header in enumerate(headers) if header[1].strip().lower() == b'log']
    if len(logs) != 1:
        raise ValueError('Logging defaults require one unambiguous Log section')
    index = logs[0]
    begin = headers[index].end()
    end = headers[index + 1].start() if index + 1 < len(headers) else len(ini)
    return begin, end


def _values(fragment, key):
    return re.findall(rb'(?mi)^[ \t]*' + key.encode() + rb'[ \t]*=[ \t]*([^;#\r\n]*)', fragment)


def apply_logging_profile(ini, profile):
    defaults = _defaults(profile)
    begin, end = _section(ini)
    fragment = ini[begin:end]
    newline = b'\r\n' if b'\r\n' in ini else b'\n'
    for key, value in defaults.items():
        if len(_values(fragment, key)) > 1:
            raise ValueError('Duplicate logging setting: ' + key)
        pattern = rb'(?mi)^([ \t]*' + key.encode() + rb'[ \t]*=[ \t]*)[^\r\n]*'
        fragment, count = re.subn(pattern, lambda match: match[1] + value.encode(), fragment)
        if not count:
            if fragment and not fragment.endswith(b'\n'):
                fragment += newline
            fragment += key.encode() + b' = ' + value.encode() + newline
    result = ini[:begin] + fragment + ini[end:]
    verify_logging_profile(result, profile)
    return result


def verify_logging_profile(ini, profile):
    defaults = _defaults(profile)
    begin, end = _section(ini)
    for key, expected in defaults.items():
        values = _values(ini[begin:end], key)
        if len(values) != 1:
            raise ValueError('Missing or ambiguous logging setting: ' + key)
        actual = values[0].strip().lower().decode('ascii')
        # All compiled automatic output defaults are false, independently covered
        # by PublicLoggingDefaults.Tests.ps1. Level and file intent are explicit.
        public_auto = profile == 'public' and key not in ('LogToFile', 'LogLevel') and actual == 'auto'
        if actual != expected and not public_auto:
            raise ValueError('Logging profile mismatch: ' + profile + '/' + key)
    return profile
