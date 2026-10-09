# Offline Character Inspector source

Character Inspector starts explicitly from NeuRotic and is off by default. The installed component lives under `OptiScaler/CharacterInspector/Version 1`.

The host and worker use protocol schema 3. Model identities are pinned in `models.lock.json`, and original source identities are retained in `upstream.lock.json`. `server.py` uses the installed locked models; acquisition is an explicit developer preparation step, not a live inference download.

The source checkout is not a complete installed runtime. Assemble the pinned Python, NumPy, OpenCV and model dependencies with the required component licenses. Preserve the runtime distribution inventory and host integrity checks. Use the complete installer bundle for the ready-to-use offline component.

Original OpenCV Zoo sources and notices remain under `third_party`. Preserve their copyright and license terms, the provider's Apache notice, and dependency notices. See the project [Credits](../../ATTRIBUTION.md) and [third-party notices](../../THIRD_PARTY_NOTICES.md).
