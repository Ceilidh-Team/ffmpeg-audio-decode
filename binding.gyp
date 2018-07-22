{
    'targets': [
        {
            'target_name': 'binding',
            'sources': [
                'src/main.c'
            ],
            'conditions': [
                ['OS in "linux mac"', {
                    'cflags!': [ '-Wno-unused-parameter' ],
                    'link_settings': {
                        'libraries': [
                            '-lavcodec.58',
                            '-lavformat.58'
                        ]
                    }
                }],
                ['OS=="mac"', {
                    'xcode_settings': {
                        'WARNING_CFLAGS!': [ '-Wno-unused-parameter' ]
                    }
                }]
            ]
        }
    ]
}