{
    'targets': [
        {
            'target_name': 'binding',
            'sources': [
                'src/main.c'
            ],
            'conditions': [
                ['OS in "linux mac"', {
                    'link_settings': {
                        'libraries': [
                            '-lavcodec.58',
                            '-lavformat.58'
                        ]
                    }
                }]
            ]
        }
    ]
}
