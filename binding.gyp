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
        "CANBridge/",
        "<!@(node -p \"require('node-addon-api').include\")"
        ],
      'defines': [
        "NAPI_VERSION=<(napi_build_version)"
      ],
      'dependencies': ["<!(node -p \"require('node-addon-api').gyp\")"],
      'libraries': [
            '<(module_root_dir)/CANBridge/x86-64/CANBridge.lib',
            '<(module_root_dir)/CANBridge/x86-64/wpiutil.lib',
            '<(module_root_dir)/CANBridge/x86-64/wpiutiljni.lib',
            '<(module_root_dir)/CANBridge/x86-64/wpiHal.lib',
            '<(module_root_dir)/CANBridge/x86-64/wpiHaljni.lib',
      ],
      'msvs_settings': {
        'VCCLCompilerTool': {
            'ExceptionHandling': 1,
            'AdditionalOptions': [ '-std:c++17' ],
            'RuntimeLibrary': 0
        },
      },
      "cflags_cc!": ["-std=c++17", '-fno-exceptions'],
      "cflags!": ["-std=c++17", '-fno-exceptions'],
    }
  ]
}
