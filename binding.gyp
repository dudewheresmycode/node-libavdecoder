{
    "targets": [
    {
        "target_name": "avdecoder",
        "sources": [
            "src/main.cc",
            "src/decoder.cc"
        ],
        'include_dirs': [
            './usr/include',
            "<!(node -e \"require('nan')\")"
        ],
        "libraries": [
            "../usr/lib/libavformat.a",
            "../usr/lib/libavfilter.a",
            "../usr/lib/libavutil.a",
            "../usr/lib/libavcodec.a",
            "../usr/lib/libswscale.a",
            "../usr/lib/libswresample.a",
            #"-L/usr/lib",
            "-lswresample","-lavformat","-lavfilter","-lavutil","-lavcodec","-lswscale","-lz", "-lm"
        ]
    }
    ]
}
