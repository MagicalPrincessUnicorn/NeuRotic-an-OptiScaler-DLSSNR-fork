# Controlled TEST002 framing input

`supervised_testonly.jsonl` is test data, never a product receipt. It retains
one raw assertion and its C15 wrapper from the ignored historical log
`x64/sp03-registration-focus-20260924-140022/sp03-initial_profile_preparation.exe.log`
(original run `e7d2f6756c0a4be9b94978a5ac460252`). The run ID was replaced with
32 `1` characters and the terminal totals reduced to the one retained row.

The row is `initial-profile-preparation/preparation_without_fake_commit`.
It checks fixture preparation without requiring C05/C06. It does not prove
N01's original real-production-activation oracle or any other Spectre
obligation. Tests write inert compiler/interpreter/executable bytes and construct
controlled command documents around this log. Nothing in those documents was
executed. Their authority contexts are TEST_ONLY except one explicitly labeled
host-API authority stub that proves even an accepted receipt cannot acquire a
product mapping. No GPU, model, game, or product gate evidence is present.
