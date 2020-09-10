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
            '-l<(module_root_dir)/CANBridge/x86-64/CANBridge.lib'
      ],
      'msvs_settings': {
        'VCCLCompilerTool': { 'ExceptionHandling': 1},
      }
    }
  ]
}
