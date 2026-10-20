# Retired schema-2 maintenance fixtures

The supported `Test-Hub.ps1` suite exercises schema 3 Install/Fresh Install/Uninstall through the shared core. History, rollback, recovery, Sanitize, Dumbfire, SHA-bound file decisions, and new prepared x86 installation are removed product operations.

The old PowerShell fixtures remain at their historical paths for research: InstallTransaction, InstallLifecycle, InstallerProtocol, InstallerPreview, InstallerFileDecisions, RecoveryReceipts, RecoveryLeafBoundary, ConflictRecovery, LargeInspection, PathSafety and the X86 install/Hub/proxy-ownership fixtures. They assert the superseded transaction/backup/prepared service and are deliberately excluded from the shipping runner. They are not passing evidence for the current installer.

`OperationFlowTests-v2.cpp` and `GameStatusTests-v2.cpp` preserve the original native maintenance assertions. Their active counterparts retain applicable checks, add retired-operation refusal, and test simple result/stat/metadata contracts. Settings encoding/concurrency, discovery, anti-cheat policy/collector/private approval, runtime deposits and path/ownership boundaries continue through supported fixtures. Legacy prepared removal is covered by SimpleLegacyInstall; unsupported new x86 installation is refused by SimpleInstallerParity.
