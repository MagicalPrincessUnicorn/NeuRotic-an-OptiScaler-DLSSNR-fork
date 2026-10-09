# Third-party notices

Components retain their original license and copyright terms. The project license is [GNU GPL version 3](LICENSE).

## Project and adapted work

NeuRotic retains the authorship of OptiScaler and Dagherbou's OptiScaler DLSS-NR fork. Magpie-derived window capture retains GPLv3 terms and corresponding-source obligations; see [Magpie-LICENSE.txt](Licenses/Magpie-LICENSE.txt). Color composition preserves the original [RenoDX attribution and MIT text](Licenses/RenoDX_ATTRIBUTION.txt). The interface palette preserves Moonlight / Madam-Herta attribution.

## Libraries and fonts

| Component | Retained text / obligation |
| --- | --- |
| Dear ImGui, Omar Cornut | [MIT license](Licenses/ImGui-LICENSE.txt). Embedded STB's separate original license blocks are preserved in [STB-notices.txt](Licenses/STB-notices.txt). |
| nlohmann JSON, Niels Lohmann | [MIT license](Licenses/nlohmann-JSON-MIT.txt). |
| SimpleIni, Brodie Thiesfield | [Original embedded MIT notice](Licenses/SimpleIni-MIT.txt). |
| unordered_dense, Martin Leitner-Ankerl | [MIT license](Licenses/unordered_dense-LICENSE.txt). |
| spdlog / bundled fmt | [Original license with fmt notice](Licenses/spdlog-LICENSE.txt). |
| magic_enum, Daniil Goncharov | [MIT license](Licenses/magic_enum-LICENSE.txt). |
| Vulkan-Headers, Khronos Group | [License index](Licenses/Vulkan-Headers-LICENSE.md); preserve applicable file-level Apache-2.0/MIT notices. |
| AMD FSR 1 spatial source | [FSR1 MIT notice](Licenses/LICENSE-FSR1.txt); separate from an FSR runtime DLL. |
| AMD FidelityFX SDK v1 | [MIT text](Licenses/FidelityFX_v1_LICENSE.md). |
| AMD Anti-Lag 2 source headers | [Original embedded MIT notice](Licenses/AntiLag2-MIT.txt). |
| NVIDIA NVAPI library | [MIT notice](Licenses/NVAPI-License.txt); this does not cover DLSS/NGX. |
| Hack font | [Hack, Bitstream Vera and DejaVu notices](Licenses/Hack-font-notices.txt). |
| Noto Sans / Noto Sans CJK | [Noto Sans OFL](Licenses/OFL-NotoSans.txt) and [Noto CJK OFL](Licenses/OFL-NotoSansCJK.txt). Windows-supplied fonts remain system assets. |
| Microsoft .NET runtime | [Runtime license](Licenses/dotnet-LICENSE.TXT) and [full third-party notices](Licenses/dotnet-THIRD-PARTY-NOTICES.TXT). |
| MFG unlock adaptations | [RTX20/30 notices](Licenses/MFGAmpereUnlock-MIT.txt), [RTX40 notices](Licenses/RTX40MFG-Unlock-MIT.txt), and [MFGAdaUnlock notice](Licenses/THIRD_PARTY_MFGAdaUnlock_LICENSE.txt). |

## Separately licensed packaged components

Graphics-component license texts: [XeSS](Licenses/XeSS_LICENSE.txt), [DirectX](Licenses/DirectX_LICENSE.txt), and [FidelityFX v2](Licenses/FidelityFX_v2_LICENSE.md).

Character Inspector uses Python, NumPy, OpenCV, and OpenCV Zoo components. Their runtime, package, and model notices accompany the components.

## User-supplied and excluded components

The NVIDIA NR model `nvngx_dlssnr.dll` is user-supplied and not bundled. NeuRotic's `nvngx.dll_dlssnr.dll` is a project-built forwarder. NVIDIA DLSS/NGX/Streamline SDK and runtime rights remain separate; this document grants no right to redistribute them or private model weights.
