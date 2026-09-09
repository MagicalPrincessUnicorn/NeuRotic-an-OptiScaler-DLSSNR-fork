# Neurotic Alpha 0.9.4 UI iteration — September 9, 2026

Experiment: `exp/alpha-0.9.4-neurotic-ui`.
Exact parent/control: `c9777d0e973ce8d3b872454873fa57aa7f6eb905`, the previously
installed tabbed UI accepted by the user. Worktree:
`C:\OptiScaler-NR-Dev\worktrees\experiments\alpha-0.9.4-neurotic-ui`.

Hypothesis: the user's next UI arrangement and offline language selection improve
accessibility without changing NR, frame generation or upscaling behavior.
Changed variable: presentation only, plus the explicitly requested persisted
menu-language preference. No shipped or live INI changes. English is the default.

Included behavior:

- Top action row, then graph visibility/language controls and graphs above tabs.
- Neurotic Alpha 0.9.4 title with originating OptiScaler version; no milestone bump.
- User-supplied Ko-fi link, support text and Buy Me a Coffee button.
- English, Spanish, French, German and Portuguese; 1,067 catalog keys per language.
  Four translations authored directly from the assistant's language knowledge,
  with translated apology, preserved technical names and English fallback.
- NR and Advanced Settings remain collapsible and initially expanded.
- Existing NR status and timing beneath Enable Neural Rendering; removed redundant
  keybind description; Apply the model follows timing; second layer controls last.
- Model resolution reset to 100 percent, clearing any pending slider edit.
- Logging first in Diagnostics and initially expanded. Model/Color/Compare and
  top-level tab order are otherwise preserved.

Offline gates: catalog format/coverage verification, standalone ImGui localization
test at three scales, UI structure checks, existing NR robustness/configuration/
dispatch/readiness suites, strict generated-patch validation and result equality,
committed clean Release/x64 build with source immutability and artifact hashes.
See the workspace build-only handoff for final commit and build evidence.

Runtime result: Inconclusive. Decision: keep experimental, pause before installation.
No new candidate, baseline or stable promotion. Earlier MFG and NR runtime limits
remain in force; a UI build does not establish new rendering compatibility.

## Working-install follow-up

The user reported the installed interface works and requested one focused layout
revision. The compact support prompt is now `Enjoying NeuRotic?` plus the existing
button, right-aligned as the final row below the tabs. Language moved immediately
to the right of Open Wiki, and the apology was removed from the UI. The primary GPU
name now shares the active game-resolution/frame-count row. New displayed text is
included in all four translated catalogs. No rendering or INI behavior changed.

This follow-up is isolated on `exp/alpha-0.9.4-neurotic-ui-polish` from installed
commit `75390eebbc38c55b6050105175c8159517dc9e9c`. Its result remains Inconclusive
until installed and inspected; the working installed commit remains the control.

After installation is authorized, inspect every page, long translated tooltips,
language switching and Save Settings/restart persistence, hide/show graphs,
default-expanded sections, model-resolution reset and second-layer placement.
Verify live timings and output against the same prior configuration. Installation
must preserve the live INI and patched NR model unless the user chooses otherwise;
that decision and game-close check belong to the later deployment step.
