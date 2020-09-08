{
  'targets': [
    {
      'target_name': 'addon',
      'sources': [
        'src/addon.cpp',
        "CANBridge/"
       ],
      'include_dirs': [
        "src/",
        "CANBridge/",
        "<!@(node -p \"require('node-addon-api').include\")"
        ],
      'defines': [
        "NAPI_VERSION=<(napi_build_version)"
      ],
      'dependencies': ["<!(node -p \"require('node-addon-api').gyp\")"],
      'libraries': ['-l<(module_root_dir)/CANBridge/CANBridge86-64.lib'],
      'cflags!': [ '-fno-exceptions' ],
      'cflags_cc!': [ '-fno-exceptions' ],
      'xcode_settings': {
        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
        'CLANG_CXX_LIBRARY': 'libc++',
        'MACOSX_DEPLOYMENT_TARGET': '10.7'
      },
      'msvs_settings': {
        'VCCLCompilerTool': { 'ExceptionHandling': 1 },
      }
    }
  ]
}
