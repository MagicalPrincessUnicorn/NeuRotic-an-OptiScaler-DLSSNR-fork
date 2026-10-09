# Interface font fallback

Noto Sans CJK SC Regular is embedded as fixed product data in the desktop App
and in-game DLL. Its SIL Open Font License 1.1 and copyright statement are in
[OFL-NotoSansCJK.txt](OFL-NotoSansCJK.txt). Source identity and hashes are in
[SOURCE.json](SOURCE.json). The font came from the official notofonts/noto-cjk
repository; it is not a language-pack asset or a downloaded user dependency.

Noto Sans supplies the extended Latin glyphs that the CJK face lacks, including
Polish letters. It is also embedded as unmodified product data. Its copyright
and SIL OFL 1.1 are in OFL-NotoSans.txt; pinned source and byte hashes are in
SOURCE-NotoSans.json. The product keeps both licenses with the fonts.

The established interface font is first, followed by available Windows Arial
and the locale-appropriate Microsoft CJK face, then Noto. Only product-owned
resources and fixed Windows font names are accepted. Both regular and bold
interface atlases receive fallbacks; the editor uses the same chain. Atlas
changes happen between frames. ImGui owns retirement of its dynamic atlas
textures; language changes do not destroy a still-associated renderer texture.

Initial layout qualification covers Latin, Cyrillic, Simplified Chinese,
Japanese and Korean. Creating another BCP 47 locale does not establish RTL,
Indic shaping, or glyph coverage for that language.
