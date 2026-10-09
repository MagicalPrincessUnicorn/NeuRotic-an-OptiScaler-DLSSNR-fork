//{{NO_DEPENDENCIES}}
// Microsoft Visual C++ generated include file.
// Used by OptiScaler.rc
//
#ifdef _DEBUG
#define VER_BUILD_DATE "Debug Build"
#define VER_BUILD_COMMIT "Debug"
#else
#include "resource_build_date.h"
#include "resource_build_commit.h"
#endif // !_DEBUG

#define VS_VERSION_INFO 1

// Next default values for new objects
//
#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE 101
#define _APS_NEXT_COMMAND_VALUE 40001
#define _APS_NEXT_CONTROL_VALUE 1001
#define _APS_NEXT_SYMED_VALUE 101
#endif
#endif

#define STRINGIZE_(s) #s
#define STRINGIZE(s) STRINGIZE_(s)

#define VER_MAJOR_VERSION 10
#define VER_MINOR_VERSION 0
#define VER_HOTFIX_VERSION 0
#define VER_BUILD_NUMBER 1

// NeuRotic has an independent public release stream. This value is compared with
// published GitHub release tags and deliberately does not follow upstream's DLL version.
#define NEUROTIC_VERSION_MAJOR 0
#define NEUROTIC_VERSION_MINOR 9
#define NEUROTIC_VERSION_PATCH 7

#define VER_DEV_RELEASE
// #define VER_PRE_RELEASE

#define VER_FILE_VERSION VER_MAJOR_VERSION, VER_MINOR_VERSION, VER_HOTFIX_VERSION, VER_BUILD_NUMBER
#define VER_FILE_VERSION_STR                                                                                           \
    STRINGIZE(VER_MAJOR_VERSION) "." STRINGIZE(VER_MINOR_VERSION) "." STRINGIZE(VER_HOTFIX_VERSION) "." STRINGIZE(VER_BUILD_NUMBER)
#define OPTI_VERSION STRINGIZE(VER_MAJOR_VERSION) "." STRINGIZE(VER_MINOR_VERSION) "." STRINGIZE(VER_HOTFIX_VERSION)

#define VER_PRODUCT_VERSION VER_FILE_VERSION

#if defined(NR_DIAG_VULKAN_NO_SUBMIT) && NR_DIAG_VULKAN_NO_SUBMIT
#define VER_PRODUCT_VERSION_STR "10.0.0-diagnostic-vulkan-no-submit (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#elif defined(NR_DIAG_VULKAN_COMMAND_ONLY) && NR_DIAG_VULKAN_COMMAND_ONLY
#define VER_PRODUCT_VERSION_STR "10.0.0-diagnostic-vulkan-command-only (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#elif defined(NR_DIAG_VULKAN_NO_OBSERVERS) && NR_DIAG_VULKAN_NO_OBSERVERS
#define VER_PRODUCT_VERSION_STR "10.0.0-diagnostic-vulkan-no-observers (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#elif defined(NR_DIAG_VULKAN_NO_AUGMENT) && NR_DIAG_VULKAN_NO_AUGMENT
#define VER_PRODUCT_VERSION_STR "10.0.0-diagnostic-vulkan-no-augmentation (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#elif defined(NR_DIAG_VULKAN_NO_LEGACY) && NR_DIAG_VULKAN_NO_LEGACY
#define VER_PRODUCT_VERSION_STR "10.0.0-diagnostic-vulkan-no-legacy (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#elif defined(NR_DIAG_VULKAN_ONLY) && NR_DIAG_VULKAN_ONLY
#define VER_PRODUCT_VERSION_STR "10.0.0-diagnostic-vulkan-only (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#elif defined(NR_DIAG_FORWARD_ONLY) && NR_DIAG_FORWARD_ONLY
#define VER_PRODUCT_VERSION_STR "10.0.0-diagnostic-forward-only (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#elif defined(VER_DEV_RELEASE)
#define VER_PRODUCT_VERSION_STR                                                                                        \
    STRINGIZE(VER_MAJOR_VERSION) "." STRINGIZE(VER_MINOR_VERSION) "." STRINGIZE(VER_HOTFIX_VERSION) "-dev (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#elif VER_PRE_RELEASE
#define VER_PRODUCT_VERSION_STR                                                                                        \
    STRINGIZE(VER_MAJOR_VERSION) "." STRINGIZE(VER_MINOR_VERSION) "." STRINGIZE(VER_HOTFIX_VERSION) "-pre" STRINGIZE(VER_BUILD_NUMBER) " (" VER_BUILD_COMMIT ") (" VER_BUILD_DATE ")"
#else
#define VER_PRODUCT_VERSION_STR                                                                                        \
    STRINGIZE(VER_MAJOR_VERSION) "." STRINGIZE(VER_MINOR_VERSION) "." STRINGIZE(VER_HOTFIX_VERSION) "-final (" VER_BUILD_COMMIT ")"
#endif // VER_PRE_RELEASE

#define VER_PRODUCT_NAME "OptiScaler v" VER_PRODUCT_VERSION_STR
