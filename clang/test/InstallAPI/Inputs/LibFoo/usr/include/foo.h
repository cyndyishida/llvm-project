#if defined(NONDarwin) 
  #define LINUX "$linux"
  #define DARWIN 
#elif defined(Darwin) 
  #define LINUX 
  #define DARWIN "$darwin" 
#else 
  #define LINUX 
  #define DARWIN 
#endif 

#if defined(Foo) 
  #define FOO "FooLib$" 
#else 
  #define FOO 
#endif 

#define __STRING(x)     #x
#define PLATFORM_ALIAS(sym)	__asm("_" FOO __STRING(sym) DARWIN LINUX)
extern int foo() PLATFORM_ALIAS(foo);
