# NeuRotic language packs, format 1

UTF-8 JSON `.nrlang`, at most 16 MiB / 65,536 entries / 32 KiB per text.
Metadata fields: `formatVersion`, stable `packId`, BCP 47 `locale`, `language`,
`name`, `author`, `version`; optional `contact`, `projectUrl`, `sourcePack` and
reported `coverage`. `desktop` and `in_game` are arrays of entries with stable
`id`, `translation`, reviewed semantic `revision`, optional `note` and canonical
translator context. A pack cannot supply code, files, fonts or executable fields.

`localization/english.json` is canonical. Its IDs are retained across editorial
changes; meaning changes increment the revision. Templates expose English,
control type, context, typed placeholders, preserved terms and line-break rules.
Never translate placeholders, filenames or preserved product terms. Named
placeholders may move; printf directives must retain types and order. Runtime
formatting never executes authored text as code.

Invalid individual translations fall through local → community → included →
English. Valid stale translations remain usable and are marked Needs review.
Unknown IDs survive editing/export as obsolete data but cannot activate controls.
Empty translations are missing, not English copies counted as completed work.

The user's `NeuRotic/HubData-v2/Languages` contains Packs, Drafts, Overrides and
sanitized Active.json. Save Draft preserves work without activation. Apply
publishes only validated current IDs. Exports flatten translated edits into one
independent pack and retain obsolete entries for round trips. The desktop reloads
on Apply; the in-game menu loads the selected catalog on its next launch.

Current implementation receipts and remaining migration/editor/font work are
recorded in the overnight ledger. CMD tools remain English.
