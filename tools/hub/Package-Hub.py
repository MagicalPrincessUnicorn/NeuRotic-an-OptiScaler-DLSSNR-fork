"""Verify complete current native assembly without creating a distribution.
Pass current product and component receipts, followed by --verify-only.
An archive requires a separately authorized explicit --output destination.
"""
from pathlib import Path
import runpy
if __name__ == '__main__':
    runpy.run_path(str(Path(__file__).resolve().parents[1] / 'packaging/customer/Build-ReleaseBundle.py'),run_name='__main__')
