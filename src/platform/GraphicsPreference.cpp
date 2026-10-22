// Driver hints must be exported by the rendering executable, rather than a static library or
// launcher. Windows/user graphics preferences can override these hybrid-GPU hints.
#if defined(_WIN32)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 1;
}
#endif
