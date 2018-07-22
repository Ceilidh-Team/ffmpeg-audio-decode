{
    'targets': [
        {
            'target_name': 'binding',
            'sources': [
                'src/main.c'
            ],
            'cflags': [
                '-std=c11'
            ],
            'link_settings': {
                'libraries': [
                    '-lavcodec.58',
                    '-lavformat.58'
                ]
            }
        }
    ]
}