/*============================================================================
 *  viz_assert.h  --  极简断言/统计工具（视觉模块测试用）
 *----------------------------------------------------------------------------
 *  设计取向：不引入任何测试框架，只做三件事
 *    1) 统计通过 / 失败 / 跳过数量
 *    2) 断言失败时打印“位置 + 期望 vs 实际”，便于定位
 *    3) 支持“字符串包含”这类针对控制台输出与 JSON 文本的断言
 *===========================================================================*/
#ifndef VIZ_ASSERT_H
#define VIZ_ASSERT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern int g_pass;
extern int g_fail;
extern int g_skip;

void section(const char *name);
void vfail(const char *fmt, ...);
void vok(const char *what);

/* 基本断言 */
#define VCHECK(cond, what) \
    do { if (cond) vok(what); else { vfail("%s:%d  %s", __FILE__, __LINE__, what); } } while (0)

/* 整数相等 */
#define VEQ_INT(what, got, want)                                            \
    do {                                                                     \
        long _g = (long)(got), _w = (long)(want);                            \
        if (_g == _w) vok(what);                                             \
        else vfail("%s:%d  %s（期望 %ld，实际 %ld）",                        \
                   __FILE__, __LINE__, what, _w, _g);                        \
    } while (0)

/* 字符串相等 */
#define VEQ_STR(what, got, want)                                             \
    do {                                                                     \
        const char *_g = (got), *_w = (want);                                \
        if (_g != NULL && _w != NULL && strcmp(_g, _w) == 0) vok(what);      \
        else vfail("%s:%d  %s（期望 \"%s\"，实际 \"%s\"）",                  \
                   __FILE__, __LINE__, what, _w ? _w : "(null)",             \
                   _g ? _g : "(null)");                                      \
    } while (0)

/* 子串包含 / 不包含 */
#define VHAS(what, hay, needle)                                              \
    do {                                                                     \
        const char *_h = (hay), *_n = (needle);                              \
        if (_h != NULL && _n != NULL && strstr(_h, _n) != NULL) vok(what);   \
        else vfail("%s:%d  %s（未找到子串 \"%s\"）",                          \
                   __FILE__, __LINE__, what, _n ? _n : "(null)");            \
    } while (0)

#define VNOHAS(what, hay, needle)                                            \
    do {                                                                     \
        const char *_h = (hay), *_n = (needle);                              \
        if (_h != NULL && _n != NULL && strstr(_h, _n) == NULL) vok(what);   \
        else vfail("%s:%d  %s（不应出现的子串 \"%s\"）",                      \
                   __FILE__, __LINE__, what, _n ? _n : "(null)");            \
    } while (0)

/* 只打印信息，不计入失败（用于性能等不稳定指标） */
void vinfo(const char *fmt, ...);
#define VSKIP(what) do { g_skip++; printf("  - SKIP %s\n", what); } while (0)

#endif /* VIZ_ASSERT_H */
