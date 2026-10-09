cmake_minimum_required(VERSION 3.23)
project(NeuRoticPreparedFlow LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Prepared flow requires Windows x64 D3D12")
endif()
set(NR_FLOW_FFX_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/FidelityFX-SDK" CACHE PATH "Existing FidelityFX SDK source (not copied)")
get_filename_component(NR_FLOW_FFX_SDK "${NR_FLOW_FFX_SDK}" ABSOLUTE)
set(FFX_INCLUDE_PATH "${NR_FLOW_FFX_SDK}/sdk/include")
set(FFX_LIB_PATH "${NR_FLOW_FFX_SDK}/sdk/libs")
set(FFX_SHARED_PATH "${NR_FLOW_FFX_SDK}/sdk/src/shared")
set(FFX_HOST_PATH "${FFX_INCLUDE_PATH}/FidelityFX/host")
set(FFX_GPU_PATH "${FFX_INCLUDE_PATH}/FidelityFX/gpu")
set(FFX_COMPONENTS_PATH "${NR_FLOW_FFX_SDK}/sdk/src/components")
set(FFX_HOST_BACKENDS_PATH "${FFX_HOST_PATH}/backends")
set(FFX_SRC_BACKENDS_PATH "${NR_FLOW_FFX_SDK}/sdk/src/backends")
set(FFX_SC_EXECUTABLE "${NR_FLOW_FFX_SDK}/sdk/tools/binary_store/FidelityFX_SC.exe")
set(FFX_PLATFORM_NAME x64)
foreach(effect ALL FSR1 FSR2 FSR3 FSR3UPSCALER FI SPD CACAO LPM BLUR VRS CAS DOF LENS PARALLEL_SORT
    DENOISER SSSR BREADCRUMBS BRIXELIZER BRIXELIZER_GI CLASSIFIER)
  set(FFX_${effect} OFF)
endforeach()
set(FFX_OF ON)
set(FFX_API_DX12 ON)
set(FFX_BUILD_AS_DLL OFF)
set(FFX_AUTO_COMPILE_SHADERS ON)
file(GLOB FFX_SHARED_SOURCES "${FFX_SHARED_PATH}/*.cpp" "${FFX_SHARED_PATH}/*.h")
file(GLOB FFX_PUBLIC_SOURCES "${FFX_HOST_PATH}/*.h")
# Use the SDK's own OF-only component, shader permutation and D3D12 backend
# targets. Do not enter its top-level build, which writes into SDK source/bin.
add_library(pixlib SHARED IMPORTED GLOBAL)
set_target_properties(pixlib PROPERTIES IMPORTED_IMPLIB "${FFX_LIB_PATH}/pix/bin/x64/WinPixEventRuntime.lib")
add_subdirectory("${FFX_COMPONENTS_PATH}/opticalflow" "ffx-opticalflow")
# SDK 1.1.2's compile_shaders_with_depfile accidentally leaves its output
# variable's literal name at the front of its dependencies. Satisfy only that
# inert entry; all 28 real generated shader headers remain required outputs.
# This avoids changing the shared SDK checkout for one experimental build.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ffx-dx12")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ffx-dx12/OPTICALFLOW_PERMUTATION_OUTPUTS" "SDK output-variable dependency marker\n")
add_subdirectory("${FFX_SRC_BACKENDS_PATH}/dx12" "ffx-dx12")
set_target_properties(ffx_opticalflow_x64 ffx_backend_dx12_x64 PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
target_compile_definitions(ffx_opticalflow_x64 PRIVATE UNICODE _UNICODE)
target_compile_definitions(ffx_backend_dx12_x64 PRIVATE UNICODE _UNICODE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/PreparedFlowConvert.hlsl" NR_FLOW_SHADER_SOURCE)
configure_file(PreparedFlowShader.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/PreparedFlowShader.h" @ONLY)
add_library(neurotic_prepared_flow STATIC PreparedFlowD3D12.cpp)
target_compile_features(neurotic_prepared_flow PUBLIC cxx_std_20)
target_compile_definitions(neurotic_prepared_flow PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(neurotic_prepared_flow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(neurotic_prepared_flow PUBLIC ffx_opticalflow_x64 ffx_backend_dx12_x64 d3d12 dxgi d3dcompiler)
# Core already contains the FSR3.1 optical-flow 1.1.1 ABI. This independently
# pinned SDK uses 1.1.2 and must never satisfy those existing symbols (or borrow
# its shared release/permutation routines). Prefix at compilation on every
# participant, without patching the shared SDK checkout or native FSR callers.
set(NR_FLOW_PRIVATE_SYMBOLS
  ffxOpticalflowResourceIsNull ffxOpticalflowContextCreate ffxOpticalflowContextDestroy
  ffxOpticalflowContextDispatch ffxOpticalflowContextGetGpuMemoryUsage ffxOpticalflowGetEffectVersion
  ffxOpticalflowGetSharedResourceDescriptions ffxSafeReleaseCopyResource ffxSafeReleasePipeline
  ffxSafeReleaseResource ffxPrintMessage ffxSetPrintMessageCallback ffxBreadcrumbsAppendList
  ffxBreadcrumbsPopList ffxAssertReport ffxAssertSetPrintingCallback opticalflowGetPermutationBlobByIndex
  opticalflowIsWave64 ffxGetPermutationBlobByIndex ffxIsWave64 FfxGetAddressModeDX12
  ffxGetDX12ResourceFlags ffxGetDX12StateFromResourceState patchDxgiFormatWithFfxUsage
  ffxGetCommandListDX12 ffxGetCommandQueueDX12 ffxGetDX12FormatFromSurfaceFormat ffxGetDX12SwapchainPtr
  ffxGetDeviceDX12 ffxGetInterfaceDX12 ffxGetPipelineDX12 ffxGetResourceDX12
  ffxGetResourceDescriptionDX12 ffxGetScratchMemorySizeDX12 ffxGetSurfaceFormatDX12
  ffxGetSwapchainDX12 ffxLoadPixDll ffxFrameInterpolationUiComposition ffxFrameInterpolationDebugPacing
  ffxConfigureFrameInterpolationSwapchainDX12 ffxCreateFrameinterpolationSwapchainDX12
  ffxCreateFrameinterpolationSwapchainForHwndDX12 ffxFrameInterpolationSwapchainGetGpuMemoryUsageDX12
  ffxGetFrameinterpolationCommandlistDX12 ffxGetFrameinterpolationTextureDX12
  ffxRegisterFrameinterpolationUiResourceDX12 ffxReplaceSwapchainForFrameinterpolationDX12
  ffxSetFrameGenerationConfigToSwapchainDX12 ffxWaitForPresents)
# The older FSR2 DX12 backend exports generic C++ helpers too. Isolate the
# complete DX12 backend helper family and its private context type, not just
# public ffx* names, so archive selection cannot merge different SDK contexts.
list(APPEND NR_FLOW_PRIVATE_SYMBOLS
  BackendContext_DX12 TIF addBarrier flushBarriers getDX12ResourcePtr
  beginMarkerDX12 endMarkerDX12 GetSDKVersionDX12 GetEffectGpuMemoryUsageDX12
  CreateBackendContextDX12 GetDeviceCapabilitiesDX12 DestroyBackendContextDX12
  CreateResourceDX12 DestroyResourceDX12 MapResourceDX12 UnmapResourceDX12
  RegisterResourceDX12 GetResourceDX12 UnregisterResourcesDX12 RegisterStaticResourceDX12
  GetResourceDescriptorDX12 StageConstantBufferDataDX12 CreatePipelineDX12
  DestroyPipelineDX12 ScheduleGpuJobDX12 ExecuteGpuJobsDX12 BreadcrumbsAllocBlockDX12
  BreadcrumbsFreeBlockDX12 BreadcrumbsWriteDX12 BreadcrumbsPrintDeviceInfoDX12
  RegisterConstantBufferAllocatorDX12 GetResourceGpuMemorySizeDX12 IsDepthDX12 IsStencilDX12
  breadcrumbsPrintDeviceInfoMemory registerStaticBufferSrv registerStaticBufferUav
  registerStaticTextureSrv registerStaticTextureUav pixBeginEventOnCommandList pixEndEventOnCommandList
  FrameInterpolationSwapChainDX12 Dx12Commands SimpleMovingAverage)
foreach(target neurotic_prepared_flow ffx_opticalflow_x64 ffx_backend_dx12_x64)
  if(MSVC)
    # Games may preload an older app-local MSVCP140. Match the core's runtime
    # mutex initialization on every independently compiled flow participant,
    # including the SDK backend's placement-constructed constant-buffer mutex.
    target_compile_definitions(${target} PUBLIC _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
  endif()
  foreach(symbol IN LISTS NR_FLOW_PRIVATE_SYMBOLS)
    target_compile_definitions(${target} PRIVATE "${symbol}=nrNative_${symbol}")
  endforeach()
endforeach()
if(MSVC)
  target_compile_options(neurotic_prepared_flow PRIVATE /W4 /WX /permissive-)
endif()
add_executable(prepared_flow_backends "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/prepared_flow_backends.cpp")
target_compile_features(prepared_flow_backends PRIVATE cxx_std_20)
target_include_directories(prepared_flow_backends PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../OptiScaler"
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(prepared_flow_backends PRIVATE neurotic_prepared_flow)
add_custom_command(TARGET prepared_flow_backends POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${FFX_LIB_PATH}/pix/bin/x64/WinPixEventRuntime.dll" "$<TARGET_FILE_DIR:prepared_flow_backends>")
enable_testing()
add_test(NAME prepared_flow_backends COMMAND prepared_flow_backends)
