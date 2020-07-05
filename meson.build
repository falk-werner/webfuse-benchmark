project('webfuse-benchmark', 'c', version: '0.1.0')

libwebsockets_dep = dependency('libwebsockets', version: '>=4.0.0', required: false)
if not libwebsockets_dep.found()
    cmake = import('cmake')
    libwebsockets = cmake.subproject('libwebsockets')
    libwebsockets_dep = libwebsockets.dependency('websockets_shared')
endif

webfuse_dep = dependency('webfuse', version: '>=0.4.0', 
    fallback: ['webfuse', 'webfuse_dep'], default_options: ['without_tests=true'])

webfuse_provider_dep = dependency('webfuse_provider', version: '>=0.4.0', 
    fallback: ['webfuse_provider', 'webfuse_provider_dep'], default_options: ['without_tests=true'])

executable('webfuse-fileprovider',
    'src/main.c',
    dependencies: [webfuse_dep, webfuse_provider_dep, libwebsockets_dep])
