#if defined(_WIN32)
#  define NATIVEWEB_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#  define NATIVEWEB_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

NATIVEWEB_TEST_EXPORT
int nativeweb_test_add(int a, int b)
{
    return a + b;
}

NATIVEWEB_TEST_EXPORT
const char* nativeweb_test_name()
{
    return "nativeweb-test-library";
}
