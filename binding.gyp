{
  'targets': [
    {
      'target_name': 'addon',
      'sources': [
        'src/addon.cc',
        'src/canWrapper.cc',
      ],
      'include_dirs': [
        "src/",
        "externalCompileTimeDeps/include",
        "<!@(node -p \"require('node-addon-api').include\")"
        ],
      'defines': [
        "NAPI_VERSION=<(napi_build_version)"
      ],
      'dependencies': ["<!(node -p \"require('node-addon-api').gyp\")"],
      'conditions': [
        ['OS=="mac"', {
            'libraries': [
                # These files were placed by download-CanBridge.mjs
                '<(module_root_dir)/externalCompileTimeDeps/libCANBridge.a',
            ],
            'copies': [{
                'destination': './build/Release',
                'files': [
                    # These files were placed in the prebuilds folder by download-CanBridge.mjs
                    '<(module_root_dir)/prebuilds/darwin-x64/libCANBridge.dylib',
                    '<(module_root_dir)/prebuilds/darwin-x64/libwpiHal.dylib',
                    '<(module_root_dir)/prebuilds/darwin-x64/libwpiutil.dylib',
                ]
            }],
        }],
        ['OS=="win"', {
            'libraries': [
                # These files were placed by download-CanBridge.mjs
                '<(module_root_dir)/externalCompileTimeDeps/CANBridge.lib',
                '<(module_root_dir)/externalCompileTimeDeps/wpiHal.lib',
                '<(module_root_dir)/externalCompileTimeDeps/wpiutil.lib',
            ],
            'copies': [{
                'destination': './build/Release',
                'files': [
                    # These files were placed in the prebuilds folder by download-CanBridge.mjs
                    '<(module_root_dir)/prebuilds/win32-x64/CANBridge.dll',
                    '<(module_root_dir)/prebuilds/win32-x64/wpiHal.dll',
                    '<(module_root_dir)/prebuilds/win32-x64/wpiutil.dll',
                ]
            }],
        }],
        ['OS=="linux"', {
            'libraries': [
                # These files were placed by download-CanBridge.mjs
                '<(module_root_dir)/externalCompileTimeDeps/libCANBridge.a',
            ],
            'copies': [{
                'destination': './build/Release',
                'files': [
                    # These files were placed in the prebuilds folder by download-CanBridge.mjs
                    '<(module_root_dir)/prebuilds/linux-x64/libCANBridge.so',
                    '<(module_root_dir)/prebuilds/linux-x64/libwpiHal.so',
                    '<(module_root_dir)/prebuilds/linux-x64/libwpiutil.so',
                ]
            }],
        }],
      ],
      'msvs_settings': {
        'VCCLCompilerTool': {
            'ExceptionHandling': 1,
            'AdditionalOptions': [ '-std:c++20' ],
            'RuntimeLibrary': 0
        },
      },
      "xcode_settings": {
          "CLANG_CXX_LANGUAGE_STANDARD": "c++20",
          "OTHER_CPLUSPLUSFLAGS": ["-fexceptions"]
      },
      "cflags_cc": ["-std=c++20", '-fexceptions'],
      "cflags": ["-std=c++20", '-fexceptions'],
    }
  ]
}
