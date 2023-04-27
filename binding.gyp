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
      'libraries': [
            '<(module_root_dir)/externalCompileTimeDeps/CANBridge.lib',
            '<(module_root_dir)/externalCompileTimeDeps/wpiHal.lib',
      ],
      'msvs_settings': {
        'VCCLCompilerTool': {
            'ExceptionHandling': 1,
            'AdditionalOptions': [ '-std:c++20' ],
            'RuntimeLibrary': 0
        },
      },
      "cflags_cc!": ["-std=c++20", '-fno-exceptions'],
      "cflags!": ["-std=c++20", '-fno-exceptions'],
    }
  ]
}
