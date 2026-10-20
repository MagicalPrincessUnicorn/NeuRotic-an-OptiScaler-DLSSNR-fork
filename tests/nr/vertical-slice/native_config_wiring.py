"""Source/config round-trip boundary only. No runtime or G2 verdict."""
import pathlib,sys
repo=pathlib.Path(__file__).resolve().parents[3]
def source(path):return (repo/path).read_text(encoding='utf-8-sig')
checks={
 'default-off':'NrOptional<bool> DlssNrNativeProtocol { false };' in source('OptiScaler/Config.h'),
 'read':'DlssNrNativeProtocol.set_from_config(readBool("DlssNr", "NativeProtocol"));' in source('OptiScaler/Config.cpp'),
 'save':'ini.SetValue("DlssNr", "NativeProtocol", GetBoolValue(Instance()->DlssNrNativeProtocol.value_for_config()).c_str());' in source('OptiScaler/Config.cpp'),
 'snapshot':'X(DlssNrNativeProtocol)' in source('OptiScaler/NrConfigSnapshot.h'),
 'shipped-off':'NativeProtocol=false' in source('integration/OptiScaler.ini'),
 'documented':'NativeProtocol' in source('Config.md'),
}
for name,matched in checks.items():print(f'{name}: {"PASS" if matched else "FAIL"}')
print('Scope: source/config wiring only; runtime/model/GPU/IQ/performance NOT_RUN')
sys.exit(0 if all(checks.values()) else 1)
