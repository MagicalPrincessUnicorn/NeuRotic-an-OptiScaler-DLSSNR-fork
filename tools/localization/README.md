# Neurotic UI localization

English (default), Spanish, French, German and Portuguese are bundled in the DLL.
Translations were directly authored by the coding assistant from its language
knowledge at the user's request. They are not professional/native-speaker-reviewed
translations. No Google or downloaded-model translation is included. Neither the
runtime nor the catalog maintenance tool calls a translation service.

`OptiScaler/menu/locales/en.json` is the frozen, indexed source inventory.
`authored.tsv` holds four translations per inventory index; `=` deliberately keeps
technical notation unchanged and `@N` reuses an earlier row. `extra.tsv` adds keys
without renumbering the original inventory. Do not reorder `en.json`.

From the repository root, with Python 3:

```
python tools/localization/catalog.py materialize
python tools/localization/catalog.py build
python tools/localization/catalog.py verify
tests\Run-Localization.cmd
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NeuroticUi.ps1
```

Materialize regenerates the keyed language JSON; build validates all keys and
format arguments and generates ASCII C++ with explicit UTF-8 byte escapes. Edit
the TSV source to retain changes across materialization, or edit keyed JSON and
run build alone. New UI strings require an explicit catalog entry. Technical
names, paths, API identifiers and unknown strings intentionally retain English.

The presentation hooks operate after ImGui has resolved widget IDs and formatted
values. Measurement and drawing share the same translated text; nested calls are
guarded. Original labels, IDs, printf arguments, configuration keys and rendering
decisions are not translated. Dynamic templates preserve numbers and translate
known nested status words. Cache size is bounded and language state is thread-local.
Arial from Windows supplies accented Latin glyphs; no font download is required.

`[Menu] Language=en|es|fr|de|pt` is persisted by Save Settings. Unknown codes fall
back to English; `pt-br` is accepted as Portuguese. The choice changes the next
menu frame. English text remains the fallback for uncatalogued future messages.

The standalone tests exercise the actual modified ImGui sources, stable IDs,
translated widths, dynamic readouts, UTF-8 glyph coverage and representative
header rows at 0.5x, 1x and 2x. They do not constitute in-game visual or NR/MFG
runtime validation. Check all tabs, tooltips, dropdowns, live readouts and saved
language after the user authorizes installation.
