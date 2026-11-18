/*============================================================================
 *  viz_module.c  --  组员 C：可视化演示模块（升级版）
 *----------------------------------------------------------------------------
 *  设计思路：把“可视化”拆成【数据出口】与【渲染实现】两层
 *
 *    C 端（本文件）只负责“算得准、导得全”：
 *      viz_render_report() : 控制台仪表盘（ANSI 彩色：目标表 / 甘特条 / 算法对比）
 *      viz_draw_ascii_map(): ASCII 俯视图，没有彩色终端也能演示
 *      viz_write_trace()   : 导出场景 + 多种算法方案 + 事件流
 *                            -> web/trace.json 与 web/trace.js
 *      draw_console_sim()  : 兼容 v1 接口的“时钟推进式”过程模拟
 *
 *    渲染端（web/ 目录，纯前端、零依赖、离线可跑）：
 *      · 3D 星域场景：目标漂移、飞船航迹、对接光束、生存环
 *      · HUD：任务时钟、生存余量、救援序列、算法对比、事件日志
 *      · 交互：轨道相机、时间轴拖拽、倍速播放、方案切换、导出 PNG
 *
 *    三个设计收益（可直接写进报告的“需求—设计一致性”小节）：
 *      1) 界面与算法解耦：换渲染器（EasyX / QT / Web）不必改算法代码；
 *      2) 数据可复现：trace 由确定性种子生成，测试用例即 trace 的输入；
 *      3) 分级演示：无图形环境的机器用 ANSI/ASCII 版，有浏览器则用 3D 版。
 *===========================================================================*/
#include "space_rescue.h"
#include <stdarg.h>

/* 输出流（定义见下方“输出流”一节）：编码转换层需要提前引用 */
static FILE *viz_out(void);

#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#  define VIZ_MKDIR_ONE(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>          /* getcwd */
#  define VIZ_MKDIR_ONE(p) mkdir(p, 0777)
#endif

/*============================================================================
 *  一、控制台编码初始化（中文不乱码的关键）
 *----------------------------------------------------------------------------
 *  问题：源码里的中文是 UTF-8 字节；而“谁来解释这些字节”在不同环境里不一样：
 *    · Windows 控制台（cmd / Windows Terminal / 双击 exe）默认按系统代码页
 *      （简体中文环境 = 936/GBK）解释 —— 直接输出 UTF-8 就是乱码；
 *    · CLion / IDEA 的 Run 窗口按 IDE 的设置解码，默认同样是系统代码页，
 *      程序即使把控制台切成 UTF-8 也没用（它不看 CP，只按自己的设置解码）；
 *    · 重定向到文件时则希望保持 UTF-8，方便用编辑器直接看。
 *
 *  因此这里把“输出编码”做成显式可配置，而不是猜：
 *    1) 环境变量 RESCUE_OUT_CP 指定输出代码页（65001=UTF-8，936=GBK）；
 *    2) 未指定时默认 UTF-8，并把控制台切到 UTF-8（真实控制台下最稳）；
 *    3) CLion 的运行配置里默认注入 RESCUE_OUT_CP=936：
 *       程序按 GBK 字节输出，IDE 按 GBK 解码，中文就正常了。
 *  输出统一走 viz_printf()，需要转码时在写入前做 UTF-8 → 目标代码页 的转换。
 *===========================================================================*/
static int g_out_cp = 65001;            /* 目标输出代码页 */

void viz_set_output_codepage(int cp)
{
    if (cp == 0) cp = 65001;
    g_out_cp = cp;
#ifdef _WIN32
    if (g_out_cp == 65001) {
        SetConsoleOutputCP(65001);      /* CP_UTF8 */
        SetConsoleCP(65001);
    }
#endif
}

void viz_console_init(void)
{
    const char *env = getenv("RESCUE_OUT_CP");
    if (env != NULL && *env != '\0') {
        int cp = atoi(env);
        if (cp > 0) { viz_set_output_codepage(cp); return; }
    }
    viz_set_output_codepage(65001);      /* 默认：UTF-8 */
}

int viz_console_codepage(void)
{
#ifdef _WIN32
    return (int)GetConsoleOutputCP();
#else
    return 0;
#endif
}

/* 当前选择的输出代码页（65001=UTF-8） */
int viz_output_codepage(void)
{
    return g_out_cp;
}

/* 把一块 UTF-8 文本写到当前输出流（必要时转成目标代码页） */
static void viz_write_bytes(const char *utf8, size_t n)
{
    if (n == 0) return;
#ifdef _WIN32
    if (g_out_cp != 65001) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)n, NULL, 0);
        if (wlen > 0) {
            wchar_t *wbuf = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
            if (wbuf != NULL) {
                int mlen;
                MultiByteToWideChar(CP_UTF8, 0, utf8, (int)n, wbuf, wlen);
                mlen = WideCharToMultiByte(g_out_cp, 0, wbuf, wlen, NULL, 0, NULL, NULL);
                if (mlen > 0) {
                    char *mbuf = (char *)malloc((size_t)mlen);
                    if (mbuf != NULL) {
                        WideCharToMultiByte(g_out_cp, 0, wbuf, wlen, mbuf, mlen, NULL, NULL);
                        fwrite(mbuf, 1, (size_t)mlen, viz_out());
                        free(mbuf);
                    }
                }
                free(wbuf);
                return;
            }
        }
    }
#endif
    fwrite(utf8, 1, n, viz_out());
}

/* 视觉模块统一的输出函数：printf 的替代品（内部处理编码） */
void viz_vprintf(const char *fmt, va_list ap)
{
    char   stackbuf[2048];
    char  *buf = stackbuf;
    int    need;
    va_list ap2;

    va_copy(ap2, ap);
    need = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    if (need < 0) { va_end(ap2); return; }
    if ((size_t)need >= sizeof(stackbuf)) {
        buf = (char *)malloc((size_t)need + 1);
        if (buf == NULL) { va_end(ap2); return; }
        vsnprintf(buf, (size_t)need + 1, fmt, ap2);
    }
    va_end(ap2);

    viz_write_bytes(buf, (size_t)need);
    if (buf != stackbuf) free(buf);
}

void viz_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    viz_vprintf(fmt, ap);
    va_end(ap);
}

/*============================================================================
 *  一、ANSI 颜色基础设施
 *===========================================================================*/
static int g_color_on = -1;

/*============================================================================
 *  一、输出流（默认 stdout，可重定向）
 *----------------------------------------------------------------------------
 *  为什么要有这个：调试/测试视觉模块时，希望把仪表盘输出抓到内存或文件里
 *  做断言（例如“报告里是否出现了算法对比表”），而不是直接糊在终端上。
 *  主程序也可以用它把报告写进文件，作为实验报告的素材。
 *===========================================================================*/
static FILE *g_out = NULL;

static FILE *viz_out(void)
{
    if (g_out == NULL) g_out = stdout;
    return g_out;
}

void viz_set_stream(FILE *f)
{
    g_out = (f != NULL) ? f : stdout;
    /* 输出不是终端时关掉 ANSI 颜色，避免颜色码污染文件/内存字符串 */
    if (g_out != stdout) g_color_on = 0;
    else                 g_color_on = -1;
}

static void viz_color_init(void)
{
    if (g_out != NULL && g_out != stdout) {   /* 重定向到文件：不要颜色码 */
        g_color_on = 0;
        return;
    }
    if (g_color_on >= 0) return;
    g_color_on = 0;
#ifdef _WIN32
    {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD  mode = 0;
        if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
            if (SetConsoleMode(h, mode | 0x0004))   /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
                g_color_on = 1;
        }
    }
#else
    g_color_on = 1;
#endif
}

#define C_RESET   (g_color_on ? "\x1b[0m"  : "")
#define C_BOLD    (g_color_on ? "\x1b[1m"  : "")
#define C_CYAN    (g_color_on ? "\x1b[96m" : "")
#define C_GREEN   (g_color_on ? "\x1b[92m" : "")
#define C_YELLOW  (g_color_on ? "\x1b[93m" : "")
#define C_RED     (g_color_on ? "\x1b[91m" : "")
#define C_MAGENTA (g_color_on ? "\x1b[95m" : "")
#define C_GREY    (g_color_on ? "\x1b[90m" : "")

/* 供主程序在“输出重定向到文件 / 不支持 ANSI”时关闭颜色 */
void viz_set_color(int enable)
{
    g_color_on = enable ? 1 : 0;
}

#define VIZ_BAR_MAX 80

/* 把 ratio(0..1) 映射成 width 个字符的进度条 */
static void viz_bar(double ratio, int width, char fill, char *buf)
{
    int i, filled;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    if (width < 1) width = 1;
    if (width > VIZ_BAR_MAX) width = VIZ_BAR_MAX;
    filled = (int)(ratio * (double)width + 0.5);
    for (i = 0; i < width; ++i) buf[i] = (i < filled) ? fill : '.';
    buf[width] = '\0';
}

/* 生成一行“甘特条”：在 [off, off+span) 区间填 fill，其余为 '.' */
static void viz_gantt_line(int total, int off, int span, char fill, char *buf)
{
    int i;
    if (total < 1) total = 1;
    if (total > VIZ_BAR_MAX) total = VIZ_BAR_MAX;
    if (off < 0) off = 0;
    if (off > total) off = total;
    if (span < 1) span = 1;
    if (off + span > total) span = total - off;
    for (i = 0; i < total; ++i) {
        if (i >= off && i < off + span) buf[i] = fill;
        else if (i == off)              buf[i] = '[';
        else                            buf[i] = '.';
    }
    buf[total] = '\0';
}

/*============================================================================
 *  一·五、终端显示宽度工具
 *----------------------------------------------------------------------------
 *  中文字符在终端里占 2 列，而 printf 的 %-8s 按字节数补齐，
 *  所以含中文的表头会错位。下面按“显示列宽”补齐，保证表头与数据对齐。
 *  注意：拷贝时按 UTF-8 字符边界整体搬运，缓冲区不够时宁可少拷一个字符，
 *        也不会把一个汉字劈成半个（否则终端里会出现乱码方块）。
 *===========================================================================*/
static int viz_utf8_clen(unsigned char c)
{
    if (c < 0x80) return 1;
    if (c < 0xC0) return 1;    /* 非法起始字节，按单字节处理，保证不死循环 */
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    return 4;
}

int viz_width(const char *s)
{
    int w = 0;
    while (s != NULL && *s) {
        unsigned char c = (unsigned char)*s;
        int cl = viz_utf8_clen(c);
        w += (cl == 1) ? 1 : 2;     /* ASCII 占 1 列，非 ASCII 占 2 列 */
        s += cl;
    }
    return w;
}

/* 把 s 拷入 dst（不切断 UTF-8 字符），返回实际拷入的字节数 */
static int viz_copy_clipped(char *dst, int dstsz, const char *s)
{
    int n = 0;
    if (dstsz <= 0) return 0;
    while (*s != '\0') {
        unsigned char c = (unsigned char)*s;
        int cl = viz_utf8_clen(c);
        if (n + cl > dstsz - 1) break;     /* 放不下就整体放弃该字符 */
        memcpy(dst + n, s, (size_t)cl);
        n += cl;
        s += cl;
    }
    dst[n] = '\0';
    return n;
}

void viz_pad_r(char *dst, int dstsz, const char *s, int width)
{
    int w, i, n;
    if (dstsz <= 0) return;
    if (s == NULL) s = "";
    w = viz_width(s);
    n = viz_copy_clipped(dst, dstsz, s);
    /* 内容被截断时不补空格：补空格会把“没放下”伪装成“已对齐”，
     * 反而让排查表格错位时看不出问题。 */
    if (n >= (int)strlen(s)) {
        for (i = w; i < width && n < dstsz - 1; ++i) dst[n++] = ' ';
    }
    dst[n] = '\0';
}

void viz_pad_l(char *dst, int dstsz, const char *s, int width)
{
    int w, i, n = 0, copied;
    if (dstsz <= 0) return;
    if (s == NULL) s = "";
    w = viz_width(s);
    if ((int)strlen(s) <= dstsz - 1) {          /* 放得下才左侧补空格 */
        for (i = w; i < width && n < dstsz - 1; ++i) dst[n++] = ' ';
    }
    copied = viz_copy_clipped(dst + n, dstsz - n, s);
    n += copied;
    dst[n] = '\0';
}

/*============================================================================
 *  二、控制台仪表盘
 *===========================================================================*/
void viz_render_report(const RescueData *rd,
                       const RoutePlan *plans, int n_plans,
                       const char *plan_titles[])
{
    int i, p;
    char bar[VIZ_BAR_MAX + 1];
    char name[64];
    char idbuf[16];
    int  best_idx = 0;

    viz_color_init();

    viz_printf("\n%s%s================================================================================%s\n",
           C_BOLD, C_CYAN, C_RESET);
    viz_printf("%s%s                    太空救援任务 · 救援方案分析报告%s\n", C_BOLD, C_CYAN, C_RESET);
    viz_printf("%s%s================================================================================%s\n\n",
           C_BOLD, C_CYAN, C_RESET);

    /* ---------- 场景概览 ---------- */
    viz_printf("%s[场景概览]%s %s\n", C_BOLD, C_RESET, scenario_name(rd->scenario));
    viz_printf("   目标数 %s%d%s | 飞船速度 %s%.1f%s | 坐标刷新间隔 t=%s%.1f%s | 救援基点 (%.0f, %.0f, %.0f)\n\n",
           C_YELLOW, rd->target_count, C_RESET,
           C_YELLOW, SPEED, C_RESET,
           C_YELLOW, UPDATE_INTERVAL, C_RESET,
           rd->origin_x, rd->origin_y, rd->origin_z);

    /* ---------- 目标清单 ---------- */
    viz_printf("%s[目标清单]%s  （压力条 = 直飞耗时占生存上限的比例，越短越安全）\n",
           C_BOLD, C_RESET);
    {
        char h1[40], h2[40], h3[40], h4[40], h5[40], h6[40], h7[48];
        viz_pad_r(h1, sizeof(h1), "编号", 5);
        viz_pad_l(h2, sizeof(h2), "X", 10);
        viz_pad_l(h3, sizeof(h3), "Y", 10);
        viz_pad_l(h4, sizeof(h4), "Z", 10);
        viz_pad_l(h5, sizeof(h5), "直飞耗时", 9);
        viz_pad_l(h6, sizeof(h6), "生存上限", 9);
        viz_pad_r(h7, sizeof(h7), "压力条", 24);
        viz_printf("  %s %s %s %s %s %s  %s\n", h1, h2, h3, h4, h5, h6, h7);
    }
    viz_printf("  %s--------------------------------------------------------------------------------%s\n",
           C_GREY, C_RESET);

    for (i = 0; i < rd->target_count; ++i) {
        const Target *tg = &rd->target_list[i];
        double d     = calc_3d_distance(rd->origin_x, rd->origin_y, rd->origin_z,
                                        tg->x, tg->y, tg->z);
        double eta   = d / SPEED;
        double ratio = (tg->life_max_time > 0.0) ? eta / tg->life_max_time : 0.0;
        const char *col;

        if (tg->is_saved)                     col = C_GREEN;
        else if (ratio > 0.65)                col = C_RED;
        else if (ratio > 0.40)                col = C_YELLOW;
        else                                  col = C_CYAN;

        snprintf(idbuf, sizeof(idbuf), "T%d", i + 1);
        viz_bar(ratio, 16, '#', bar);
        viz_printf("  %-5s %s%10.2f %10.2f %10.2f%s %9.2f %9.2f  %s%s %3.0f%%%s%s\n",
               idbuf,
               C_GREY, tg->x, tg->y, tg->z, C_RESET,
               eta, tg->life_max_time,
               col, bar, ratio * 100.0, tg->is_saved ? " √" : "  ", C_RESET);
    }

    /* ---------- 推荐方案 ---------- */
    best_idx = viz_choose_best(plans, n_plans);
    if (n_plans > 0 && best_idx >= 0) {
        const RoutePlan *bp = &plans[best_idx];

        plan_name(bp->algo, name, (int)sizeof(name));
        viz_printf("\n%s[推荐方案]%s %s%s%s\n", C_BOLD, C_RESET, C_MAGENTA, name, C_RESET);
        viz_printf("   获救 %s%d%s/%d   总航程 %s%.2f%s   完成时刻 %s%.2f%s   可行：%s\n",
               C_GREEN, bp->saved, C_RESET, rd->target_count,
               C_CYAN, bp->length, C_RESET,
               C_CYAN, bp->finish_time, C_RESET,
               bp->feasible ? "全部获救" : "存在牺牲");

        viz_printf("   救援顺序：%s基点%s", C_GREY, C_RESET);
        for (i = 0; i < bp->len; ++i)
            viz_printf(" %s→%s %sT%d%s", C_GREY, C_RESET, C_BOLD, bp->seq[i] + 1, C_RESET);
        viz_printf("\n\n");

        /* 航段甘特条 */
        viz_printf("   %s航段时序甘特图%s  （横轴 = 任务时间 0 ~ %.2f）\n", C_BOLD, C_RESET, bp->finish_time);
        for (i = 0; i < bp->len; ++i) {
            double t0 = (i == 0) ? 0.0 : bp->leg_t[i - 1];
            double t1 = bp->leg_t[i];
            int    off, span;

            if (bp->finish_time > 0.0) {
                off  = (int)(40.0 * t0 / bp->finish_time + 0.5);
                span = (int)(40.0 * (t1 - t0) / bp->finish_time + 0.5);
            } else { off = 0; span = 40; }
            if (span < 1) span = 1;

            viz_gantt_line(40, off, span, '=', bar);
            viz_printf("    T%-2d %s%s%s %6.2f ~ %6.2f   航程 %7.2f\n",
                   bp->seq[i] + 1, C_GREEN, bar, C_RESET, t0, t1, bp->leg_d[i]);
        }
    }

    /* ---------- 算法对比 ---------- */
    if (n_plans > 0) {
        char h1[48], h2[32], h3[32], h4[32], h5[32], h6[32], h7[32];

        viz_printf("\n%s[算法对比]%s\n", C_BOLD, C_RESET);
        viz_pad_r(h1, sizeof(h1), "策略", 24);
        viz_pad_l(h2, sizeof(h2), "获救", 6);
        viz_pad_l(h3, sizeof(h3), "牺牲", 6);
        viz_pad_l(h4, sizeof(h4), "可行", 6);
        viz_pad_l(h5, sizeof(h5), "总航程", 11);
        viz_pad_l(h6, sizeof(h6), "搜索节点", 12);
        viz_pad_l(h7, sizeof(h7), "耗时(ms)", 12);
        viz_printf("  %s %s %s %s %s %s %s\n", h1, h2, h3, h4, h5, h6, h7);
        viz_printf("  %s--------------------------------------------------------------------------------%s\n",
               C_GREY, C_RESET);

        for (p = 0; p < n_plans; ++p) {
            const RoutePlan *pl = &plans[p];
            char pname[64], pname2[64], feas[16];
            const char *title = (plan_titles != NULL) ? plan_titles[p] : NULL;

            if (title != NULL) snprintf(pname, sizeof(pname), "%s", title);
            else if (pl->label[0] != '\0') snprintf(pname, sizeof(pname), "%s", pl->label);
            else               plan_name(pl->algo, pname, (int)sizeof(pname));

            viz_pad_r(pname2, sizeof(pname2), pname, 24);
            viz_pad_l(feas, sizeof(feas), pl->feasible ? "是" : "否", 6);

            viz_printf("  %s %s%6d%s %6d %s %11.2f %12ld %12.4f  %s%s%s\n",
                   pname2,
                   (pl->saved == rd->target_count) ? C_GREEN : C_YELLOW, pl->saved, C_RESET,
                   pl->dead,
                   feas,
                   pl->length, pl->nodes, pl->elapsed_ms,
                   (p == best_idx) ? C_YELLOW : "", (p == best_idx) ? "★ 推荐" : "", C_RESET);
        }
        viz_printf("  %s--------------------------------------------------------------------------------%s\n",
               C_GREY, C_RESET);
        viz_printf("  %s结论%s：贪心 O(n^2) 快但可能牺牲目标；回溯在 n≤%d 时给出“获救数最大、航程最短”的解；"
               "2-opt 在贪心解上做局部改进，质量与耗时的折中。\n",
               C_BOLD, C_RESET, MAX_TARGET_NUM);
    }

    viz_printf("\n%s提示：./space_rescue --trace 会生成 web/trace.js，"
           "浏览器打开 web/index.html 即可观看 3D 救援演示。%s\n\n",
           C_GREY, C_RESET);
}

/*============================================================================
 *  三、ASCII 俯视图（XZ 平面投影）
 *===========================================================================*/
void viz_draw_ascii_map(const RescueData *rd)
{
    enum { MAP_W = 72, MAP_H = 22 };
    char   grid[MAP_H][MAP_W + 1];
    double minx = 0.0, maxx = 0.0, minz = 0.0, maxz = 0.0;
    int    i, r, c;

    viz_color_init();

    for (i = 0; i < rd->target_count; ++i) {
        const Target *tg = &rd->target_list[i];
        if (i == 0) { minx = maxx = tg->x; minz = maxz = tg->z; }
        if (tg->x < minx) minx = tg->x;
        if (tg->x > maxx) maxx = tg->x;
        if (tg->z < minz) minz = tg->z;
        if (tg->z > maxz) maxz = tg->z;
    }
    if (minx > 0.0) minx = 0.0;
    if (maxx < 0.0) maxx = 0.0;
    if (minz > 0.0) minz = 0.0;
    if (maxz < 0.0) maxz = 0.0;
    if (maxx - minx < 1e-6) { minx -= 1.0; maxx += 1.0; }
    if (maxz - minz < 1e-6) { minz -= 1.0; maxz += 1.0; }
    {
        double padx = (maxx - minx) * 0.10 + 2.0;
        double padz = (maxz - minz) * 0.10 + 2.0;
        minx -= padx; maxx += padx;
        minz -= padz; maxz += padz;
    }

    for (r = 0; r < MAP_H; ++r) {
        for (c = 0; c < MAP_W; ++c) grid[r][c] = (r % 4 == 0) ? ((c % 8 == 0) ? '+' : '-') : ' ';
        grid[r][MAP_W] = '\0';
    }

    /* 基点 */
    {
        int ox = (int)((0.0 - minx) / (maxx - minx) * (MAP_W - 1) + 0.5);
        int oz = (int)((0.0 - minz) / (maxz - minz) * (MAP_H - 1) + 0.5);
        if (ox >= 0 && ox < MAP_W && oz >= 0 && oz < MAP_H) grid[oz][ox] = '@';
    }

    /* 目标 */
    for (i = 0; i < rd->target_count; ++i) {
        const Target *tg = &rd->target_list[i];
        int tx = (int)((tg->x - minx) / (maxx - minx) * (MAP_W - 1) + 0.5);
        int tz = (int)((tg->z - minz) / (maxz - minz) * (MAP_H - 1) + 0.5);
        char ch = tg->is_saved ? 'o' : (char)('0' + ((i + 1) % 10));
        if (tx >= 0 && tx < MAP_W && tz >= 0 && tz < MAP_H) grid[tz][tx] = ch;
    }

    viz_printf("\n%s[俯视图 · XZ 投影]%s   @ = 救援基点   数字 = 目标编号   o = 已获救\n",
           C_BOLD, C_RESET);
    for (r = 0; r < MAP_H; ++r) viz_printf("   %s%s%s\n", C_GREY, grid[r], C_RESET);
    viz_printf("   X ∈ [%.1f, %.1f]    Z ∈ [%.1f, %.1f]\n\n", minx, maxx, minz, maxz);
}

/*============================================================================
 *  四、控制台救援过程模拟（兼容 v1 接口）
 *----------------------------------------------------------------------------
 *  注意：本函数在“目标状态的副本”上推进，绝不修改 rd 里的初始坐标。
 *        否则随后 viz_write_trace 导出的就不再是 t=0 的初始坐标，
 *        网页端按 p0+v·t 还原动画时就会出现“航线与目标对不上”的错位。
 *===========================================================================*/
void draw_console_sim(RescueData *rd)
{
    Target snap[MAX_TARGET_NUM];
    int    i, k;
    double t = 0.0;
    char   bar[24];

    viz_color_init();
    viz_printf("\n%s[救援过程模拟]%s  （终端逐帧推进，等价于图形界面的时间轴）\n",
           C_BOLD, C_RESET);

    memcpy(snap, rd->target_list, sizeof(Target) * (size_t)rd->target_count);
    for (i = 0; i < rd->target_count; ++i) snap[i].life_left = snap[i].life_max_time;

    for (i = 0; i < MAX_TARGET_NUM; ++i) {
        int    idx = rd->best_seq[i];
        double tau;

        if (idx < 0 || idx >= rd->target_count) break;
        if (!rd->target_list[idx].is_saved) continue;

        tau = rd->target_list[idx].save_time - t;
        if (tau < 0.0) tau = 0.0;

        for (k = 0; k < rd->target_count; ++k) update_target_pos(&snap[k], tau);
        t = rd->target_list[idx].save_time;

        viz_bar(1.0 - snap[idx].life_left / snap[idx].life_max_time, 16, '=', bar);
        viz_printf("  %sT+%6.2f%s → 追踪 %sT%-2d%s 剩余生存 %s%6.2f%s [%s%s%s] %s√ 已对接%s\n",
               C_CYAN, t, C_RESET,
               C_BOLD, idx + 1, C_RESET,
               (snap[idx].life_left < 10.0) ? C_RED : C_GREEN,
               snap[idx].life_left, C_RESET,
               C_GREY, bar, C_RESET,
               C_GREEN, C_RESET);
    }
    viz_printf("  %s任务结束：T+%.2f   获救 %d/%d%s\n\n",
           C_GREY, t, rd->saved_count, rd->target_count, C_RESET);
}

/*============================================================================
 *  五、trace 导出（JSON / JS）—— Web 渲染的数据契约
 *===========================================================================*/
/* 文件头：数据来源标记（写进 trace 的 meta.dataSource） */
static const char *g_data_source = "full";

void viz_set_data_source(const char *src)
{
    g_data_source = (src != NULL && *src != '\0') ? src : "full";
}

static void viz_num(FILE *f, double v)
{
    if (fabs(v) < 1e-9) v = 0.0;
    fprintf(f, "%.3f", v);
}

/* 场景名（供打印与 trace 共用） */
const char *scenario_name(int sc)
{
    switch (sc) {
        case SCENARIO_TIGHT:   return "紧急场景（生存时间紧张）";
        case SCENARIO_CLUSTER: return "星团场景（目标聚集在远处）";
        case SCENARIO_WIDE:    return "大尺度场景（散布范围大）";
        case SCENARIO_RANDOM:  return "完全随机场景";
        default:               return "标准场景";
    }
}

/* 选择推荐方案：获救数优先 → 航程 → 耗时 */
int viz_choose_best(const RoutePlan *plans, int n_plans)
{
    int i, best = 0;
    if (plans == NULL || n_plans <= 0) return -1;
    for (i = 1; i < n_plans; ++i) {
        if (plans[i].saved > plans[best].saved) { best = i; continue; }
        if (plans[i].saved < plans[best].saved) continue;
        if (plans[i].length < plans[best].length - 1e-9) { best = i; continue; }
        if (fabs(plans[i].length - plans[best].length) < 1e-9 &&
            plans[i].elapsed_ms < plans[best].elapsed_ms) best = i;
    }
    return best;
}

/* 把 trace 主体写入 FILE（json 与 js 复用同一份内容） */
static void viz_write_body(FILE *f,
                           const RescueData *rd,
                           const RoutePlan *plans, int n_plans,
                           int selected)
{
    char   tsbuf[64];
    time_t now = time(NULL);
    struct tm *tinfo = localtime(&now);
    int    i, p, k;

    if (tinfo != NULL) strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", tinfo);
    else               snprintf(tsbuf, sizeof(tsbuf), "unknown");

    fprintf(f, "{\n");

    /* ---------- meta ---------- */
    fprintf(f, "  \"meta\": {\n");
    fprintf(f, "    \"title\": \"太空救援任务 · 最优救援序列可视化\",\n");
    fprintf(f, "    \"generated\": \"%s\",\n", tsbuf);
    fprintf(f, "    \"scenario\": %d,\n", rd->scenario);
    fprintf(f, "    \"scenarioName\": \"%s\",\n", scenario_name(rd->scenario));
    fprintf(f, "    \"seed\": %u,\n", rd->seed);
    fprintf(f, "    \"count\": %d,\n", rd->target_count);
    fprintf(f, "    \"speed\": ");          viz_num(f, SPEED);
    fprintf(f, ",\n    \"updateInterval\": "); viz_num(f, UPDATE_INTERVAL);
    fprintf(f, ",\n    \"maxTargets\": %d,\n", MAX_TARGET_NUM);
    fprintf(f, "    \"origin\": [");
    viz_num(f, rd->origin_x); fprintf(f, ", ");
    viz_num(f, rd->origin_y); fprintf(f, ", ");
    viz_num(f, rd->origin_z); fprintf(f, "],\n");
    fprintf(f, "    \"dataSource\": \"%s\",\n", g_data_source);
    fprintf(f, "    \"selectedPlan\": %d\n", selected);
    fprintf(f, "  },\n");

    /* ---------- targets ---------- */
    if (rd->target_count <= 0) {
        fprintf(f, "  \"targets\": [],\n");     /* 空数组写成一行，便于肉眼阅读 */
    } else {
        fprintf(f, "  \"targets\": [\n");
        for (i = 0; i < rd->target_count; ++i) {
            const Target *tg = &rd->target_list[i];
            double d = calc_3d_distance(rd->origin_x, rd->origin_y, rd->origin_z,
                                        tg->x, tg->y, tg->z);
            fprintf(f, "    { \"id\": %d, \"name\": \"T%d\", \"p0\": [", i, i + 1);
            viz_num(f, tg->x); fprintf(f, ", ");
            viz_num(f, tg->y); fprintf(f, ", ");
            viz_num(f, tg->z); fprintf(f, "],\n      \"v\": [");
            fprintf(f, "%.4f, %.4f, %.4f", tg->vx, tg->vy, tg->vz);
            fprintf(f, "], \"life\": ");  viz_num(f, tg->life_max_time);
            fprintf(f, ", \"dist0\": ");  viz_num(f, d);
            fprintf(f, ", \"eta0\": ");   viz_num(f, d / SPEED);
            fprintf(f, " }%s\n", (i + 1 < rd->target_count) ? "," : "");
        }
        fprintf(f, "  ],\n");
    }

    /* ---------- plans ---------- */
    fprintf(f, "  \"plans\": [\n");
    for (p = 0; p < n_plans; ++p) {
        const RoutePlan *pl = &plans[p];
        char name[64];

        if (pl->label[0] != '\0') snprintf(name, sizeof(name), "%s", pl->label);
        else                      plan_name(pl->algo, name, (int)sizeof(name));

        fprintf(f, "    {\n");
        fprintf(f, "      \"id\": %d, \"algo\": %d, \"name\": \"%s\",\n", p, pl->algo, name);
        fprintf(f, "      \"saved\": %d, \"dead\": %d, \"feasible\": %d,\n",
                pl->saved, pl->dead, pl->feasible);
        fprintf(f, "      \"length\": ");   viz_num(f, pl->length);
        fprintf(f, ", \"finish\": ");       viz_num(f, pl->finish_time);
        fprintf(f, ", \"nodes\": %ld", pl->nodes);
        fprintf(f, ", \"elapsedMs\": %.3f,\n", pl->elapsed_ms);

        fprintf(f, "      \"route\": [");
        for (k = 0; k < pl->len; ++k) fprintf(f, "%s%d", (k ? ", " : ""), pl->seq[k]);
        fprintf(f, "],\n");

        fprintf(f, "      \"legs\": [");
        for (k = 0; k < pl->len; ++k) {
            double t0 = (k == 0) ? 0.0 : pl->leg_t[k - 1];
            fprintf(f, "%s{ \"from\": %d, \"to\": %d, \"t0\": ", (k ? ", " : ""),
                    (k == 0) ? -1 : pl->seq[k - 1], pl->seq[k]);
            viz_num(f, t0);
            fprintf(f, ", \"t1\": ");   viz_num(f, pl->leg_t[k]);
            fprintf(f, ", \"dist\": "); viz_num(f, pl->leg_d[k]);
            fprintf(f, " }");
        }
        fprintf(f, "],\n");

        /* 每个目标的结局（UI 上色用） */
        fprintf(f, "      \"outcome\": [");
        for (k = 0; k < rd->target_count; ++k) {
            int li, found = -1;
            for (li = 0; li < pl->len; ++li)
                if (pl->seq[li] == k) { found = li; break; }

            if (k) fprintf(f, ", ");
            if (found >= 0) {
                fprintf(f, "{ \"id\": %d, \"state\": \"saved\", \"t\": ", k);
                viz_num(f, pl->leg_t[found]);
            } else {
                fprintf(f, "{ \"id\": %d, \"state\": \"dead\", \"t\": ", k);
                viz_num(f, rd->target_list[k].life_max_time);
            }
            fprintf(f, " }");
        }
        fprintf(f, "]\n");
        fprintf(f, "    }%s\n", (p + 1 < n_plans) ? "," : "");
    }
    fprintf(f, "  ],\n");

    /* ---------- events ---------- */
    {
        const RoutePlan *sel = (selected >= 0 && selected < n_plans) ? &plans[selected] : NULL;

        fprintf(f, "  \"events\": [\n");
        fprintf(f, "    { \"t\": 0, \"type\": \"launch\", \"target\": -1, "
                   "\"text\": \"救援飞船离开发射基点\" }");

        if (sel != NULL) {
            double t = UPDATE_INTERVAL;
            int    round = 1;

            while (t < sel->finish_time + 1e-9) {
                fprintf(f, ",\n    { \"t\": ");
                viz_num(f, t);
                fprintf(f, ", \"type\": \"refresh\", \"target\": -1, "
                           "\"text\": \"收到目标最新坐标（第 %d 次刷新）\" }", round);
                t += UPDATE_INTERVAL;
                round++;
            }

            for (k = 0; k < sel->len; ++k) {
                fprintf(f, ",\n    { \"t\": ");
                viz_num(f, sel->leg_t[k]);
                fprintf(f, ", \"type\": \"saved\", \"target\": %d, "
                           "\"text\": \"T%d 完成对接，生存时间锁定\" }", sel->seq[k], sel->seq[k] + 1);
            }

            for (k = 0; k < rd->target_count; ++k) {
                int li, found = 0;
                for (li = 0; li < sel->len; ++li)
                    if (sel->seq[li] == k) found = 1;
                if (!found) {
                    fprintf(f, ",\n    { \"t\": ");
                    viz_num(f, rd->target_list[k].life_max_time);
                    fprintf(f, ", \"type\": \"dead\", \"target\": %d, "
                               "\"text\": \"T%d 生存时间耗尽，救援失败\" }", k, k + 1);
                }
            }

            fprintf(f, ",\n    { \"t\": ");
            viz_num(f, sel->finish_time);
            fprintf(f, ", \"type\": \"finish\", \"target\": -1, \"text\": \"救援任务结束\" }");
        }
        fprintf(f, "\n  ]\n");
    }

    fprintf(f, "}\n");
}

/*----------------------------------------------------------------------------
 *  viz_mkdir_p：逐级创建目录（类似 mkdir -p），已存在则忽略。
 *  这样 viz_write_trace 可以直接写到 "out/case1/deep" 这种多级新目录。
 *--------------------------------------------------------------------------*/
static void viz_mkdir_p(const char *path)
{
    char   buf[512];
    size_t i, n;

    if (path == NULL) return;
    n = strlen(path);
    if (n == 0 || n >= sizeof(buf)) return;
    memcpy(buf, path, n + 1);

    for (i = 1; i <= n; ++i) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            char save = buf[i];
            buf[i] = '\0';
            (void)VIZ_MKDIR_ONE(buf);      /* 失败（含“已存在”）都忽略 */
            buf[i] = save;
        }
    }
}

/*----------------------------------------------------------------------------
 *  路径工具：把相对路径补全成绝对路径，并在输出里醒目标出
 *----------------------------------------------------------------------------
 *  为什么需要：trace 写到哪里取决于“当前工作目录”（CWD）。CLion 的 CMake
 *  运行配置默认用构建目录作为 CWD（cmake-build-debug），命令行则常用工程根目录，
 *  于是同一个参数写出来的文件可能落在不同位置。把绝对路径打印出来，
 *  “trace 在哪”这个问题就永远有答案。
 *--------------------------------------------------------------------------*/
static void viz_abs_path(const char *path, char *out, int outsz)
{
    if (outsz <= 0) return;
#ifdef _WIN32
    if (_fullpath(out, path, (size_t)outsz) == NULL) {
        snprintf(out, (size_t)outsz, "%s", path);
    }
#else
    {
        char cwd[400];
        if (path[0] == '/') {
            snprintf(out, (size_t)outsz, "%s", path);
        } else if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(out, (size_t)outsz, "%s/%s", cwd, path);
        } else {
            snprintf(out, (size_t)outsz, "%s", path);
        }
    }
#endif
}

/* 当前工作目录（用于在输出里说明“相对路径是相对谁”） */
void viz_get_cwd(char *out, int outsz)
{
    if (outsz <= 0) return;
#ifdef _WIN32
    if (_getcwd(out, outsz) == NULL) snprintf(out, (size_t)outsz, "?");
#else
    if (getcwd(out, (size_t)outsz) == NULL) snprintf(out, (size_t)outsz, "?");
#endif
}

/* 默认导出目录：环境变量 RESCUE_OUT_DIR 优先，否则用相对目录 "out"。
 * CLion 的运行配置里注入了绝对路径，这样无论 CWD 是什么都能写对地方。 */
const char *viz_default_out_dir(void)
{
    const char *env = getenv("RESCUE_OUT_DIR");
    if (env != NULL && *env != '\0') return env;
    return "out";
}

/*----------------------------------------------------------------------------
 *  viz_write_trace：写出 <out_dir>/trace.json 与 <out_dir>/trace.js
 *  trace.js 形式为 window.RESCUE_TRACE = {...};
 *  这样双击打开 index.html（file:// 协议）时不会触发浏览器的跨域限制。
 *--------------------------------------------------------------------------*/
int viz_write_trace(const RescueData *rd,
                    const RoutePlan *plans, int n_plans,
                    const char *out_dir)
{
    char  json_path[512], js_path[512], dir[400];
    char  abs_dir[600], abs_json[600], abs_js[600], cwd[400];
    FILE *f;
    int   selected;

    if (rd == NULL || plans == NULL || n_plans <= 0) return -1;
    if (out_dir == NULL || *out_dir == '\0') out_dir = viz_default_out_dir();
    snprintf(dir, sizeof(dir), "%s", out_dir);

    selected = viz_choose_best(plans, n_plans);

    viz_mkdir_p(dir);

    snprintf(json_path, sizeof(json_path), "%s/trace.json", dir);
    snprintf(js_path,   sizeof(js_path),   "%s/trace.js",   dir);

    viz_abs_path(dir, abs_dir, (int)sizeof(abs_dir));
    viz_abs_path(json_path, abs_json, (int)sizeof(abs_json));
    viz_abs_path(js_path, abs_js, (int)sizeof(abs_js));
    viz_get_cwd(cwd, (int)sizeof(cwd));

    f = fopen(json_path, "wb");
    if (f == NULL) {
        viz_printf("[可视化模块] 无法写入 %s\n", json_path);
        return -2;
    }
    viz_write_body(f, rd, plans, n_plans, selected);
    fclose(f);

    f = fopen(js_path, "wb");
    if (f == NULL) {
        viz_printf("[可视化模块] 无法写入 %s\n", js_path);
        return -3;
    }
    fprintf(f, "/* 本文件由 space_rescue 自动生成，请勿手工编辑。\n"
               " * 打开同目录下的 index.html 即可离线观看 3D 救援演示。 */\n");
    fprintf(f, "window.RESCUE_TRACE = ");
    viz_write_body(f, rd, plans, n_plans, selected);
    fprintf(f, ";\n");
    fclose(f);

    viz_printf("\n[可视化模块] 已生成（绝对路径，直接复制到资源管理器即可）：\n");
    viz_printf("               JSON: %s\n", abs_json);
    viz_printf("               JS  : %s\n", abs_js);
    viz_printf("[可视化模块] 这次的工作目录是：%s\n", cwd);
    if (strstr(abs_dir, "\\web") != NULL || strstr(abs_dir, "/web") != NULL) {
        viz_printf("[可视化模块] 用浏览器打开 %s\\index.html 即可查看 3D 演示"
                   "（页面读的就是上面这个 trace.js）。\n\n", abs_dir);
    } else {
        viz_printf("[可视化模块] 预览方式：\n"
                   "               ① 打开 viz_test\\preview.html（它读 viz_test\\out\\trace.js）；\n"
                   "               ② 或把上面的 trace.js 复制到 web\\ 下，再打开 web\\index.html。\n\n");
    }
    return 0;
}
