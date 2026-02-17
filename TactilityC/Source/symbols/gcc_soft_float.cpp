#include <sdkconfig.h>
#ifndef CONFIG_IDF_TARGET_ESP32P4

#include <private/elf_symbol.h>
#include <cstddef>

#include <symbols/gcc_soft_float.h>

#include <cstdlib>

// Reference: https://gcc.gnu.org/onlinedocs/gccint/Soft-float-library-routines.html

extern "C" {

extern float __addsf3(float a, float b);
extern double __adddf3(double a, double b);
// extern long double __addtf3(long double a, long double b);
// extern long double __addxf3(long double a, long double b);

extern float __subsf3(float a, float b);
extern double __subdf3(double a, double b);
// extern long double __subtf3(long double a, long double b);
// extern long double __subxf3(long double a, long double b);

extern float __mulsf3(float a, float b);
extern double __muldf3(double a, double b);
// extern long double __multf3(long double a, long double b);
// extern long double __mulxf3(long double a, long double b);

extern float __divsf3(float a, float b);
extern double __divdf3(double a, double b);
// extern long double __divtf3(long double a, long double b);
// extern long double __divxf3(long double a, long double b);

extern float __negsf2(float a);
extern double __negdf2(double a);
// extern long double __negtf2(long double a);
// extern long double __negxf2(long double a);

extern double __extendsfdf2(float a);
// extern long double __extendsftf2(float a);
// extern long double __extendsfxf2(float a);
// extern long double __extenddftf2(double a);
// extern long double __extenddfxf2(double a);

// extern double __truncxfdf2(long double a);
// extern double __trunctfdf2(long double a);
// extern float __truncxfsf2(long double a);
// extern float __trunctfsf2(long double a);
extern float __truncdfsf2(double a);

extern int __fixsfsi(float a);
extern int __fixdfsi(double a);
// extern int __fixtfsi(long double a);
// extern int __fixxfsi(long double a);

extern long __fixsfdi(float a);
extern long __fixdfdi(double a);
// extern long __fixtfdi(long double a);
// extern long __fixxfdi(long double a);

// extern long long __fixsfti(float a);
// extern long long __fixdfti(double a);
// extern long long __fixtfti(long double a);
// extern long long __fixxfti(long double a);

extern unsigned int __fixunssfsi(float a);
extern unsigned int __fixunsdfsi(double a);
// extern unsigned int __fixunstfsi(long double a);
// extern unsigned int __fixunsxfsi(long double a);

extern unsigned long __fixunssfdi(float a);
extern unsigned long __fixunsdfdi(double a);
// extern unsigned long __fixunstfdi(long double a);
// extern unsigned long __fixunsxfdi(long double a);

// extern unsigned long long __fixunssfti(float a);
// extern unsigned long long __fixunsdfti(double a);
// extern unsigned long long __fixunstfti(long double a);
// extern unsigned long long __fixunsxfti(long double a);

extern float __floatsisf(int i);
extern double __floatsidf(int i);
// extern long double __floatsitf(int i);
// extern long double __floatsixf(int i);

extern float __floatdisf(long i);
extern double __floatdidf(long i);
// extern long double __floatditf(long i);
// extern long double __floatdixf(long i);

// extern float __floattisf(long long i);
// extern double __floattidf(long long i);
// extern long double __floattitf(long long i);
// extern long double __floattixf(long long i);

extern float __floatunsisf(unsigned int i);
extern double __floatunsidf(unsigned int i);
// extern long double __floatunsitf(unsigned int i);
// extern long double __floatunsixf(unsigned int i);

extern float __floatundisf(unsigned long i);
extern double __floatundidf(unsigned long i);
// extern long double __floatunditf(unsigned long i);
// extern long double __floatundixf(unsigned long i);

// extern float __floatuntisf(unsigned long long i);
// extern double __floatuntidf(unsigned long long i);
// extern long double __floatuntitf(unsigned long long i);
// extern long double __floatuntixf(unsigned long long i);

float __powisf2(float a, int b);
double __powidf2(double a, int b);
// long double __powitf2(long double a, int b);
// long double __powixf2(long double a, int b);

// int __cmpsf2(float a, float b);
int __cmpdf2(double a, double b);
// int __cmptf2(long double a, long double b);

int __unordsf2(float a, float b);
int __unorddf2(double a, double b);
// int __unordtf2(long double a, long double b);

int __eqsf2(float a, float b);
int __eqdf2(double a, double b);
// int __eqtf2(long double a, long double b);

int __nesf2(float a, float b);
int __nedf2(double a, double b);
// int __netf2(long double a, long double b);

int __gesf2(float a, float b);
int __gedf2(double a, double b);
// int __getf2(long double a, long double b);

int __ltsf2(float a, float b);
int __ltdf2(double a, double b);
// int __lttf2(long double a, long double b);

int __lesf2(float a, float b);
int __ledf2(double a, double b);
// int __letf2(long double a, long double b);

int __gtsf2(float a, float b);
int __gtdf2(double a, double b);
// int __gttf2(long double a, long double b);

// GCC integer arithmetic helpers (needed on 32-bit targets for 64-bit ops)
long long __divdi3(long long a, long long b);
long long __moddi3(long long a, long long b);
unsigned long long __udivdi3(unsigned long long a, unsigned long long b);
unsigned long long __umoddi3(unsigned long long a, unsigned long long b);

} // extern "C"

const esp_elfsym gcc_soft_float_symbols[] = {
    ESP_ELFSYM_EXPORT(__addsf3),
    ESP_ELFSYM_EXPORT(__adddf3),
    // ESP_ELFSYM_EXPORT(__addtf3),
    // ESP_ELFSYM_EXPORT(__addxf3),

    ESP_ELFSYM_EXPORT(__subsf3),
    ESP_ELFSYM_EXPORT(__subdf3),
    // ESP_ELFSYM_EXPORT(__subtf3),
    // ESP_ELFSYM_EXPORT(__subxf3),

    ESP_ELFSYM_EXPORT(__mulsf3),
    ESP_ELFSYM_EXPORT(__muldf3),
    // ESP_ELFSYM_EXPORT(__multf3),
    // ESP_ELFSYM_EXPORT(__mulxf3),

    ESP_ELFSYM_EXPORT(__divsf3),
    ESP_ELFSYM_EXPORT(__divdf3),
    // ESP_ELFSYM_EXPORT(__divtf3),
    // ESP_ELFSYM_EXPORT(__divxf3),

    ESP_ELFSYM_EXPORT(__negsf2),
    ESP_ELFSYM_EXPORT(__negdf2),
    // ESP_ELFSYM_EXPORT(__negtf2),
    // ESP_ELFSYM_EXPORT(__negxf2),

    ESP_ELFSYM_EXPORT(__extendsfdf2),
    // ESP_ELFSYM_EXPORT(__extendsftf2),
    // ESP_ELFSYM_EXPORT(__extendsfxf2),
    // ESP_ELFSYM_EXPORT(__extenddftf2),
    // ESP_ELFSYM_EXPORT(__extenddfxf2),

    // ESP_ELFSYM_EXPORT(__truncxfdf2),
    // ESP_ELFSYM_EXPORT(__trunctfdf2),
    // ESP_ELFSYM_EXPORT(__truncxfsf2),
    // ESP_ELFSYM_EXPORT(__trunctfsf2),
    ESP_ELFSYM_EXPORT(__truncdfsf2),

    ESP_ELFSYM_EXPORT(__fixsfsi),
    ESP_ELFSYM_EXPORT(__fixdfsi),
    // ESP_ELFSYM_EXPORT(__fixtfsi),
    // ESP_ELFSYM_EXPORT(__fixxfsi),

    ESP_ELFSYM_EXPORT(__fixsfdi),
    ESP_ELFSYM_EXPORT(__fixdfdi),
    // ESP_ELFSYM_EXPORT(__fixtfdi),
    // ESP_ELFSYM_EXPORT(__fixxfdi),

    // ESP_ELFSYM_EXPORT(__fixsfti),
    // ESP_ELFSYM_EXPORT(__fixdfti),
    // ESP_ELFSYM_EXPORT(__fixtfti),
    // ESP_ELFSYM_EXPORT(__fixxfti),

    ESP_ELFSYM_EXPORT(__fixunssfsi),
    ESP_ELFSYM_EXPORT(__fixunsdfsi),
    // ESP_ELFSYM_EXPORT(__fixunstfsi),
    // ESP_ELFSYM_EXPORT(__fixunsxfsi),

    ESP_ELFSYM_EXPORT(__fixunssfdi),
    ESP_ELFSYM_EXPORT(__fixunsdfdi),
    // ESP_ELFSYM_EXPORT(__fixunstfdi),
    // ESP_ELFSYM_EXPORT(__fixunsxfdi),

    // ESP_ELFSYM_EXPORT(__fixunssfti),
    // ESP_ELFSYM_EXPORT(__fixunsdfti),
    // ESP_ELFSYM_EXPORT(__fixunstfti),
    // ESP_ELFSYM_EXPORT(__fixunsxfti),

    ESP_ELFSYM_EXPORT(__floatsisf),
    ESP_ELFSYM_EXPORT(__floatsidf),
    // ESP_ELFSYM_EXPORT(__floatsitf),
    // ESP_ELFSYM_EXPORT(__floatsixf),

    ESP_ELFSYM_EXPORT(__floatdisf),
    ESP_ELFSYM_EXPORT(__floatdidf),
    // ESP_ELFSYM_EXPORT(__floatditf),
    // ESP_ELFSYM_EXPORT(__floatdixf),

    // ESP_ELFSYM_EXPORT(__floattisf),
    // ESP_ELFSYM_EXPORT(__floattidf),
    // ESP_ELFSYM_EXPORT(__floattitf),
    // ESP_ELFSYM_EXPORT(__floattixf),

    ESP_ELFSYM_EXPORT(__floatunsisf),
    ESP_ELFSYM_EXPORT(__floatunsidf),
    // ESP_ELFSYM_EXPORT(__floatunsitf),
    // ESP_ELFSYM_EXPORT(__floatunsixf),

    ESP_ELFSYM_EXPORT(__floatundisf),
    ESP_ELFSYM_EXPORT(__floatundidf),
    // ESP_ELFSYM_EXPORT(__floatunditf),
    // ESP_ELFSYM_EXPORT(__floatundixf),

    // ESP_ELFSYM_EXPORT(__floatuntisf),
    // ESP_ELFSYM_EXPORT(__floatuntidf),
    // ESP_ELFSYM_EXPORT(__floatuntitf),
    // ESP_ELFSYM_EXPORT(__floatuntixf),

    ESP_ELFSYM_EXPORT(__powisf2),
    ESP_ELFSYM_EXPORT(__powidf2),
    // ESP_ELFSYM_EXPORT(__powitf2),
    // ESP_ELFSYM_EXPORT(__powixf2),

    // ESP_ELFSYM_EXPORT(__cmpsf2),
    // ESP_ELFSYM_EXPORT(__cmpdf2),
    // ESP_ELFSYM_EXPORT(__cmptf2),

    ESP_ELFSYM_EXPORT(__unordsf2),
    ESP_ELFSYM_EXPORT(__unorddf2),
    // ESP_ELFSYM_EXPORT(__unordtf2),

    ESP_ELFSYM_EXPORT(__eqsf2),
    ESP_ELFSYM_EXPORT(__eqdf2),
    // ESP_ELFSYM_EXPORT(__eqtf2),

    ESP_ELFSYM_EXPORT(__nesf2),
    ESP_ELFSYM_EXPORT(__nedf2),
    // ESP_ELFSYM_EXPORT(__netf2),

    ESP_ELFSYM_EXPORT(__gesf2),
    ESP_ELFSYM_EXPORT(__gedf2),
    // ESP_ELFSYM_EXPORT(__getf2),

    ESP_ELFSYM_EXPORT(__ltsf2),
    ESP_ELFSYM_EXPORT(__ltdf2),
    // ESP_ELFSYM_EXPORT(__lttf2),

    ESP_ELFSYM_EXPORT(__lesf2),
    ESP_ELFSYM_EXPORT(__ledf2),
    // ESP_ELFSYM_EXPORT(__letf2),

    ESP_ELFSYM_EXPORT(__gtsf2),
    ESP_ELFSYM_EXPORT(__gtdf2),
    // ESP_ELFSYM_EXPORT(__gttf2),

    // GCC integer arithmetic helpers
    ESP_ELFSYM_EXPORT(__divdi3),
    ESP_ELFSYM_EXPORT(__moddi3),
    ESP_ELFSYM_EXPORT(__udivdi3),
    ESP_ELFSYM_EXPORT(__umoddi3),

    ESP_ELFSYM_END
};

#endif