{
  'targets': [
    {
      'target_name': 'node_gifblobber',
      'sources': [
        'src/main.cc', 'src/slurp.cc', 'src/stretch.cc'
      ],
      'dependencies': [
        'deps/giflib-5.0.0/binding.gyp:giflib'
      ]
    }
  ]
}
