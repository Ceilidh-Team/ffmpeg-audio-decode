{
    'targets': [
        {
            'target_name': 'binding',
            'sources': [
                'src/c/main.c'
            ],
            'cflags': [
                '-std=c11'
            ],
            'link_settings': {
                'libraries': [
                    '-lavcodec.58.18.100',
                    '-lavformat.58.12.100'
                ]
            }
        }
    ]
}