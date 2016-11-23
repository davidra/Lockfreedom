///////////////////////////////////////////////////////////////////////////
//
//debug.h
//
// utilities for debugging
// 
/////////////////////////////////////////////////////////////////////////////
#pragma once

#if defined _DEBUG
	#define LF_assert(expr, ...) \
				do																		\
				{																		\
					if (!(expr)) {														\
						debug::WriteLine("Assert in %s(%d): "  #expr ":" __VA_ARGS__);	\
						debug::WriteLine("\t" __VA_ARGS__);								\
						__debugbreak();													\
					}																	\
				}																		\
				while (false)
#else
	#define LF_assert(expr, ...) ((void)(expr))
#endif

//-------------------------------------------------------------------------
namespace lockfree
{
	namespace debug
	{
		void WriteLine(const char* fmt, ...);
	}
}