
#define RENDERER_DEBUG_LABELS 1

#define OGL_USE_REVERSE_DEPTH 0
#define OGL_USE_DEDICATED_GPU 0
#define RENDERER_WINDOW_FBO_NO_DEPTH 0 // 1 if all 3d rendering happens in an FBO anyway (usually with HDR via float)

#if   BUILD_DEBUG

	#define RENDERER_PROFILING					1
	#define RENDERER_DEBUG_OUTPUT				1
	#define RENDERER_DEBUG_OUTPUT_BREAKPOINT	1
	#define OGL_STATE_ASSERT					1
	#define IMGUI_DEMO							1

#elif BUILD_VALIDATE

	#define RENDERER_PROFILING					1
	#define RENDERER_DEBUG_OUTPUT				1
	#define RENDERER_DEBUG_OUTPUT_BREAKPOINT	0
	#define OGL_STATE_ASSERT					0

#elif BUILD_TRACY

	//#define NDEBUG // no asserts

	#define RENDERER_PROFILING					1 // Could impact perf? Maybe disable this?

#elif BUILD_RELEASE

	//#define NDEBUG // no asserts

#endif
