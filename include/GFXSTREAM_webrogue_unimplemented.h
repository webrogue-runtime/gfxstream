#define GFXSTREAM_NOT_IMPLEMENTED { \
    fprintf(stderr, "this function is not implemented\n"); \
    fprintf(stderr, "__FILE__: %s\n", __FILE__); \
    fprintf(stderr, "__LINE__: %d\n", __LINE__); \
    fprintf(stderr, "__FUNCTION__: %s\n", __FUNCTION__); \
    fprintf(stderr, "__PRETTY_FUNCTION__: %s\n", __PRETTY_FUNCTION__); \
    \
    __builtin_unreachable(); \
}