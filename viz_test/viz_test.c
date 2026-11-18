/*============================================================================
 *  viz_test.c  --  视觉模块（队员 C）独立测试与调试程序
 *----------------------------------------------------------------------------
 *  这个程序不属于正式工程，它的唯一目的是：**只加载 viz_module.c**，
 *  用假数据把每个可视化接口单独喂一遍，方便调试与写测试报告。
 *
 *  运行方式（详见本目录 README.md）：
 *      build_test.bat            编译（默认：只含视觉模块 + 假数据）
 *      viz_test.exe              跑全部测试，输出 PASS/FAIL 汇总
 *      viz_test.exe --demo       交互式沙箱：改场景 -> 立刻看输出
 *      viz_test.exe --emit out   生成一份 trace 供 preview.html 预览
 *
 *  依赖：space_rescue.h / ../viz_module.c / mock_data.c
 *===========================================================================*/
#include "space_rescue.h"
#include "mock_data.h"
#include "viz_assert.h"

/* 建目录：测试程序只用来准备 out/ 目录 */
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>          /* 代码页转换：MultiByteToWideChar / WideCharToMultiByte */
#  include <direct.h>
#  define TEST_MKDIR(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  define TEST_MKDIR(p) mkdir((p), 0777)
#endif

/*============================================================================
 *  一、断言统计
 *===========================================================================*/
int g_pass = 0;
int g_fail = 0;
int g_skip = 0;

void vok(const char *what)
{
    g_pass++;
    viz_printf("  \xE2\x88\x9A %s\n", what);      /* √ */
}

void vfail(const char *fmt, ...)
{
    va_list ap;
    g_fail++;
    viz_printf("  \xC3\x97 ");
    va_start(ap, fmt);
    viz_vprintf(fmt, ap);
    va_end(ap);
    viz_printf("\n");
}

void vinfo(const char *fmt, ...)
{
    va_list ap;
    viz_printf("    · ");
    va_start(ap, fmt);
    viz_vprintf(fmt, ap);
    va_end(ap);
    viz_printf("\n");
}

void section(const char *name)
{
    viz_printf("\n[%s]\n", name);
}

/*============================================================================
 *  二、通用工具：捕获输出流、读写文件、JSON 粗检
 *===========================================================================*/
static FILE *g_capfp = NULL;

/* 把 cp 代码页的文本转成 UTF-8（供断言比较）；失败返回 NULL */
static char *cap_to_utf8(const char *src, int cp)
{
#ifdef _WIN32
    int    wlen, ulen;
    wchar_t *wbuf;
    char  *ubuf;

    wlen = MultiByteToWideChar((UINT)cp, 0, src, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    wbuf = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
    if (wbuf == NULL) return NULL;
    MultiByteToWideChar((UINT)cp, 0, src, -1, wbuf, wlen);

    ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
    if (ulen <= 0) { free(wbuf); return NULL; }
    ubuf = (char *)malloc((size_t)ulen);
    if (ubuf == NULL) { free(wbuf); return NULL; }
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, ubuf, ulen, NULL, NULL);
    free(wbuf);
    return ubuf;
#else
    (void)src; (void)cp;
    return NULL;
#endif
}

/* 开始捕获：把视觉模块的输出重定向到临时文件（内存/磁盘临时文件均可） */
static void cap_start(void)
{
    g_capfp = tmpfile();
    if (g_capfp == NULL) {
        viz_printf("  ! 无法创建临时文件，捕获类测试将跳过\n");
        return;
    }
    viz_set_stream(g_capfp);
}

/* 结束捕获：返回捕获到的文本（调用者负责 free），并恢复 stdout。
 *
 * 注意：视觉模块的输出编码取决于 RESCUE_OUT_CP（CLion 下默认 936=GBK）。
 * 而测试断言里的期望字符串是源码里的 UTF-8 字面量，所以这里统一把捕获到的
 * 字节转回 UTF-8，保证断言在“UTF-8 模式”和“GBK 模式”下都成立。
 */
static char *cap_end(void)
{
    long   n;
    char  *buf;
    int    cp;

    if (g_capfp == NULL) return NULL;
    fflush(g_capfp);
    fseek(g_capfp, 0, SEEK_END);
    n = ftell(g_capfp);
    fseek(g_capfp, 0, SEEK_SET);
    if (n < 0) n = 0;
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(g_capfp);
        g_capfp = NULL;
        viz_set_stream(NULL);
        return NULL;
    }
    if (n > 0 && fread(buf, 1, (size_t)n, g_capfp) != (size_t)n) {
        buf[0] = '\0';          /* 读短不算致命，按已读长度处理 */
    }
    buf[n] = '\0';
    fclose(g_capfp);
    g_capfp = NULL;
    viz_set_stream(NULL);       /* 恢复 stdout，并让颜色检测重新生效 */

    /* 非 UTF-8 输出模式下，把内容转回 UTF-8，供断言比较 */
    cp = viz_output_codepage();
    if (cp != 65001 && n > 0) {
        char *conv = cap_to_utf8(buf, cp);
        if (conv != NULL) {
            free(buf);
            buf = conv;
        }
    }
    return buf;
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    long  n;
    char *buf;

    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) buf[0] = '\0';
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int file_ok(const char *path)
{
    FILE *f = fopen(path, "rb");
    long  n;
    if (f == NULL) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n > 0;
}

static int count_sub(const char *hay, const char *needle)
{
    int n = 0;
    const char *p = hay;
    size_t len;

    if (hay == NULL || needle == NULL || *needle == '\0') return 0;
    len = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

/* 极简 JSON 结构检查：括号配平 + 引号成对（够用于“文本是否写坏”的判断） */
static int braces_balanced(const char *s)
{
    int depth = 0, i;
    if (s == NULL) return 0;
    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '{' || s[i] == '[') depth++;
        else if (s[i] == '}' || s[i] == ']') {
            depth--;
            if (depth < 0) return 0;
        }
    }
    return depth == 0;
}

static void rm_dir_file(const char *path)
{
    (void)remove(path);
}

/*============================================================================
 *  三、测试用场景构造
 *===========================================================================*/
/* 3 个静止目标、顺序 0→1→2 全部获救（用于绝大多数断言，数值可预测） */
static void scene_small(RescueData *rd, RoutePlan plans[1], int *n_plans)
{
    int order[3];

    scene_reset(rd, SCENARIO_STANDARD, 1234u);
    scene_add(rd, 100.0, 0.0, 0.0, 200.0);      /* T1：直飞 10t */
    scene_add(rd, 0.0, 60.0, 0.0, 200.0);       /* T2：直飞 6t  */
    scene_add(rd, 0.0, 0.0, 80.0, 200.0);       /* T3：直飞 8t  */

    order[0] = 0; order[1] = 1; order[2] = 2;
    mock_build_plan(rd, order, 3, ALGO_NN, &plans[0]);
    *n_plans = 1;
}

/* 9 目标环形场景 + 三条不同顺序的方案（模拟“多算法对比”的界面效果） */
static void scene_full(RescueData *rd, RoutePlan plans[4], int *n_plans)
{
    int order[9];
    int i;

    scene_reset(rd, SCENARIO_TIGHT, 2026u);
    scene_ring(rd, 9, 140.0, 180.0, 12.0);

    for (i = 0; i < 9; ++i) order[i] = i;
    mock_build_plan(rd, order, 9, ALGO_NN, &plans[0]);

    for (i = 0; i < 9; ++i) order[i] = 8 - i;          /* 反向：故意很差 */
    mock_build_plan(rd, order, 9, ALGO_URGENCY, &plans[1]);

    for (i = 0; i < 9; ++i) order[i] = (i % 2 == 0) ? i : 8 - i;  /* 交替 */
    mock_build_plan(rd, order, 9, ALGO_2OPT, &plans[2]);

    mock_nearest_plan(rd, &plans[3]);                   /* 最近邻 */
    plans[3].algo = ALGO_BACKTRACK;
    *n_plans = 4;
}

/*============================================================================
 *  四、1. 文本宽度与对齐工具
 *===========================================================================*/
static void test_text_tools(void)
{
    char buf[32];
    char small[5];
    char tiny[2];

    section("1. 文本宽度与对齐工具（中文占 2 列）");

    VEQ_INT("viz_width(\"abc\")=3",      viz_width("abc"), 3);
    VEQ_INT("viz_width(\"中文\")=4",      viz_width("中文"), 4);
    VEQ_INT("viz_width(\"T3 状态\")=7",   viz_width("T3 状态"), 7);
    VEQ_INT("viz_width(\"\")=0",          viz_width(""), 0);
    VEQ_INT("viz_width(NULL)=0",          viz_width(NULL), 0);

    viz_pad_r(buf, (int)sizeof(buf), "ab", 5);
    VEQ_STR("pad_r 右补空格", buf, "ab   ");
    VEQ_INT("pad_r 后显示宽度=5", viz_width(buf), 5);

    viz_pad_l(buf, (int)sizeof(buf), "ab", 5);
    VEQ_STR("pad_l 左补空格", buf, "   ab");

    viz_pad_r(buf, (int)sizeof(buf), "中文", 6);
    VEQ_STR("pad_r 中文右补空格", buf, "中文  ");
    VEQ_INT("pad_r 中文后显示宽度=6", viz_width(buf), 6);

    viz_pad_l(buf, (int)sizeof(buf), "中文", 6);
    VEQ_STR("pad_l 中文左补空格", buf, "  中文");

    viz_pad_r(buf, (int)sizeof(buf), "abcdef", 3);
    VEQ_STR("宽度小于内容时原样保留", buf, "abcdef");

    /* 小缓冲区：不能把一个汉字劈成半个（否则终端出现乱码方块） */
    viz_pad_r(small, (int)sizeof(small), "中文中文", 20);
    VEQ_INT("小缓冲区长度≤4 字节", (long)strlen(small) <= 4, 1);
    VEQ_INT("小缓冲区只放整字符（显示宽度=2）", viz_width(small), 2);

    viz_pad_r(tiny, 1, "abc", 5);
    VEQ_STR("dstsz=1 只写字符串结束符", tiny, "");
}

/*============================================================================
 *  五、2. 推荐方案选择规则
 *===========================================================================*/
static void test_choose_best(void)
{
    RoutePlan p[4];

    section("2. 推荐方案选择（获救数 → 航程 → 耗时）");

    memset(p, 0, sizeof(p));

    /* 获救数优先：p1 虽然航程更长，但多救 1 个，应胜出 */
    p[0].saved = 8; p[0].length = 900.0;  p[0].elapsed_ms = 1.0;
    p[1].saved = 9; p[1].length = 1200.0; p[1].elapsed_ms = 50.0;
    VEQ_INT("获救数优先", viz_choose_best(p, 2), 1);

    /* 同获救数比航程：p2 更短 */
    p[0].saved = 9; p[0].length = 1000.0; p[0].elapsed_ms = 30.0;
    p[1].saved = 9; p[1].length = 1200.0; p[1].elapsed_ms = 10.0;
    VEQ_INT("同获救数时航程短者优先", viz_choose_best(p, 2), 0);

    /* 获救数、航程都相同 → 比耗时 */
    p[0].elapsed_ms = 30.0;
    p[1].length = 1000.0; p[1].elapsed_ms = 5.0;
    VEQ_INT("航程相同时耗时少者优先", viz_choose_best(p, 2), 1);

    /* 完全相同 → 取第一个（稳定，不抖动） */
    p[1].elapsed_ms = 30.0;
    VEQ_INT("完全相同则取第一个", viz_choose_best(p, 2), 0);

    VEQ_INT("n_plans=0 返回 -1",  viz_choose_best(p, 0), -1);
    VEQ_INT("plans=NULL 返回 -1", viz_choose_best(NULL, 3), -1);
}

/*============================================================================
 *  六、3. 控制台仪表盘
 *===========================================================================*/
static void test_report(void)
{
    RescueData rd;
    RoutePlan  plans[4];
    int        n_plans = 0;
    char      *out;

    section("3. 控制台仪表盘 viz_render_report");

    scene_small(&rd, plans, &n_plans);
    {
        const char *titles[1] = { "最近邻贪心 NN" };
        cap_start();
        viz_render_report(&rd, plans, n_plans, titles);
        out = cap_end();
    }
    if (out == NULL) { VSKIP("无法捕获输出"); return; }

    VHAS("包含报告标题", out, "救援方案分析报告");
    VHAS("包含场景概览", out, "[场景概览]");
    VHAS("包含目标清单", out, "[目标清单]");
    VHAS("包含推荐方案", out, "[推荐方案]");
    VHAS("包含算法对比", out, "[算法对比]");
    VHAS("标出推荐方案", out, "\xE2\x98\x85 推荐");
    VHAS("列出目标 T1", out, "T1");
    VHAS("列出目标 T3", out, "T3");
    VHAS("含航段甘特图", out, "航段时序甘特图");
    VNOHAS("输出到文件时不含 ANSI 颜色码", out, "\x1b[");
    free(out);

    /* 无方案：不应崩溃，且不出现“算法对比”段 */
    cap_start();
    viz_render_report(&rd, NULL, 0, NULL);
    out = cap_end();
    VCHECK(out != NULL, "n_plans=0 时仍能输出");
    if (out != NULL) {
        VHAS("n_plans=0 仍有场景概览", out, "[场景概览]");
        VNOHAS("n_plans=0 不输出算法对比", out, "[算法对比]");
        free(out);
    }
}

/*============================================================================
 *  七、4. ASCII 俯视图
 *===========================================================================*/
static void test_ascii_map(void)
{
    RescueData rd;
    char      *out;

    section("4. ASCII 俯视图 viz_draw_ascii_map");

    /* 正常 3 目标 */
    scene_reset(&rd, SCENARIO_STANDARD, 1u);
    scene_add(&rd, 100.0, 0.0, 0.0, 100.0);
    scene_add(&rd, -50.0, 0.0, 60.0, 100.0);
    scene_add(&rd, 0.0, 0.0, -80.0, 100.0);
    rd.target_list[0].is_saved = 1;

    cap_start();
    viz_draw_ascii_map(&rd);
    out = cap_end();
    if (out != NULL) {
        VHAS("含俯视图标题", out, "[俯视图");
        VHAS("标出救援基点 @", out, "@");
        VHAS("标出已获救标记 o", out, "o");
        VNOHAS("坐标范围不含 nan", out, "nan");
        VNOHAS("坐标范围不含 inf", out, "inf");
        free(out);
    }

    /* 单目标恰好在原点：不能除零 */
    scene_reset(&rd, SCENARIO_STANDARD, 2u);
    scene_add(&rd, 0.0, 0.0, 0.0, 50.0);
    cap_start();
    viz_draw_ascii_map(&rd);
    out = cap_end();
    VCHECK(out != NULL, "单目标（位于原点）不崩溃");
    if (out != NULL) { VNOHAS("单目标不含 nan", out, "nan"); free(out); }

    /* 所有目标重叠在同一点 */
    scene_reset(&rd, SCENARIO_STANDARD, 3u);
    scene_add(&rd, 10.0, 10.0, 10.0, 50.0);
    scene_add(&rd, 10.0, 10.0, 10.0, 50.0);
    scene_add(&rd, 10.0, 10.0, 10.0, 50.0);
    cap_start();
    viz_draw_ascii_map(&rd);
    out = cap_end();
    VCHECK(out != NULL, "目标全部重叠时仍能绘制");
    if (out != NULL) { VNOHAS("重叠场景不含 nan", out, "nan"); free(out); }

    /* 零目标 */
    scene_reset(&rd, SCENARIO_STANDARD, 4u);
    cap_start();
    viz_draw_ascii_map(&rd);
    out = cap_end();
    VCHECK(out != NULL, "零目标不崩溃");
    if (out != NULL) { VNOHAS("零目标不含 nan", out, "nan"); free(out); }
}

/*============================================================================
 *  八、5. 控制台过程模拟（重点：不允许修改入参）
 *===========================================================================*/
static void test_console_sim(void)
{
    RescueData rd, snap;
    RoutePlan  plans[1];
    int        n_plans = 0;
    char      *out;

    section("5. 控制台过程模拟 draw_console_sim");

    scene_small(&rd, plans, &n_plans);
    {
        int i;
        for (i = 0; i < plans[0].len; ++i) {
            int idx = plans[0].seq[i];
            rd.target_list[idx].is_saved  = 1;
            rd.target_list[idx].save_time = plans[0].leg_t[i];
            rd.best_seq[i] = idx;
        }
        for (i = plans[0].len; i < MAX_TARGET_NUM; ++i) rd.best_seq[i] = -1;
        rd.saved_count = plans[0].saved;
    }

    memcpy(&snap, &rd, sizeof(RescueData));

    cap_start();
    draw_console_sim(&rd);
    out = cap_end();

    if (out != NULL) {
        VHAS("含模拟标题", out, "[救援过程模拟]");
        VHAS("含对接提示", out, "已对接");
        VHAS("含任务结束", out, "任务结束");
        free(out);
    }

    /* 回归测试：过程模拟必须在“副本”上推进，不能改动调用者的初始坐标。
     * 曾经的反例：直接 update_target_pos(&rd->target_list[k], tau)，
     * 导致随后导出的 trace 坐标整体漂移，网页里航线与目标对不上。 */
    VCHECK(memcmp(&snap, &rd, sizeof(RescueData)) == 0,
           "过程模拟不修改调用者的 RescueData（坐标/状态零污染）");

    /* best_seq 全是 -1：没有可演示的航段 */
    scene_small(&rd, plans, &n_plans);
    memset(rd.best_seq, 0xFF, sizeof(rd.best_seq));
    rd.saved_count = 0;
    cap_start();
    draw_console_sim(&rd);
    out = cap_end();
    VCHECK(out != NULL, "无航段时不崩溃");
    if (out != NULL) { VHAS("无航段时仍打印结束行", out, "任务结束"); free(out); }

    /* best_seq 含越界下标 */
    scene_small(&rd, plans, &n_plans);
    rd.best_seq[0] = 99;
    cap_start();
    draw_console_sim(&rd);
    out = cap_end();
    VCHECK(out != NULL, "best_seq 越界时不崩溃");
    if (out != NULL) free(out);

    /* 零目标 */
    scene_reset(&rd, SCENARIO_STANDARD, 9u);
    cap_start();
    draw_console_sim(&rd);
    out = cap_end();
    VCHECK(out != NULL, "零目标时不崩溃");
    if (out != NULL) free(out);
}

/*============================================================================
 *  九、6. trace 导出：正常路径与结构检查
 *===========================================================================*/
static void test_trace_write(void)
{
    RescueData rd;
    RoutePlan  plans[1];
    int        n_plans = 0;
    int        rc;
    char      *json;
    char      *out;
    const char *dir = "out/tt_case1";

    section("6. trace 导出 viz_write_trace（正常路径）");

    scene_small(&rd, plans, &n_plans);

    /* 先跑一次过程模拟，验证导出的仍是 t=0 初始坐标（防回归） */
    {
        int i;
        for (i = 0; i < plans[0].len; ++i) {
            int idx = plans[0].seq[i];
            rd.target_list[idx].is_saved  = 1;
            rd.target_list[idx].save_time = plans[0].leg_t[i];
            rd.best_seq[i] = idx;
        }
        for (i = plans[0].len; i < MAX_TARGET_NUM; ++i) rd.best_seq[i] = -1;
    }
    cap_start();
    draw_console_sim(&rd);
    out = cap_end();
    if (out != NULL) free(out);

    rc = viz_write_trace(&rd, plans, n_plans, dir);
    VEQ_INT("返回 0 表示成功", rc, 0);
    VCHECK(file_ok("out/tt_case1/trace.json"), "生成 trace.json 且非空");
    VCHECK(file_ok("out/tt_case1/trace.js"),   "生成 trace.js 且非空");

    json = read_all("out/tt_case1/trace.json");
    VCHECK(json != NULL, "trace.json 可读");
    if (json != NULL) {
        VCHECK(braces_balanced(json), "JSON 括号配平");
        VHAS("含 meta", json, "\"meta\"");
        VHAS("含 targets 数组", json, "\"targets\"");
        VHAS("含 plans 数组", json, "\"plans\"");
        VHAS("含 events 数组", json, "\"events\"");
        VHAS("含 launch 事件", json, "\"type\": \"launch\"");
        VHAS("含 finish 事件", json, "\"type\": \"finish\"");

        /* 坐标必须是初始坐标：T1 是 (100,0,0)，飞行 95.58/10 之类都不该出现 */
        VHAS("导出的是初始坐标(t=0)", json, "\"p0\": [100.000, 0.000, 0.000]");

        /* 顺序 0→1→2 应原样出现在 route 里 */
        VHAS("route 顺序正确", json, "\"route\": [0, 1, 2]");

        /* legs 数量 = 3 段 */
        VEQ_INT("legs 段数=3", count_sub(json, "\"from\": "), 3);

        /* legs 的 dist 应为 SPEED*时长；T1 距离 100 → 10t → 100.000 */
        VHAS("首段距离 100.000", json, "\"dist\": 100.000");

        free(json);
    }

    /* trace.js 的包裹形式 */
    json = read_all("out/tt_case1/trace.js");
    if (json != NULL) {
        VHAS("trace.js 以 window.RESCUE_TRACE 开头", json, "window.RESCUE_TRACE = {");
        VCHECK(braces_balanced(json), "trace.js 括号配平");
        free(json);
    }

    /* 多级新目录自动创建 */
    rc = viz_write_trace(&rd, plans, n_plans, "out/tt_nested/a/b");
    VEQ_INT("多级目录自动创建", rc, 0);
    VCHECK(file_ok("out/tt_nested/a/b/trace.json"), "嵌套目录里生成了 trace.json");

    /* 写入失败（目标路径的父级其实是个文件） */
    {
        FILE *f = fopen("out/tt_blocker", "wb");
        if (f != NULL) { fputs("x", f); fclose(f); }
        rc = viz_write_trace(&rd, plans, n_plans, "out/tt_blocker/sub");
        VCHECK(rc < 0, "路径不可写时返回负数而不是崩溃");
        rm_dir_file("out/tt_blocker");
    }

    /* 参数非法保护 */
    VEQ_INT("rd=NULL 返回 -1",         viz_write_trace(NULL, plans, n_plans, dir), -1);
    VEQ_INT("plans=NULL 返回 -1",      viz_write_trace(&rd, NULL, n_plans, dir), -1);
    VEQ_INT("n_plans=0 返回 -1",       viz_write_trace(&rd, plans, 0, dir), -1);
}

/*============================================================================
 *  十、7. trace 导出：边界数据
 *===========================================================================*/
static void test_trace_edges(void)
{
    RescueData rd;
    RoutePlan  plans[2];
    char      *json;

    section("7. trace 导出的边界数据");

    /* (a) 空方案：一个目标都没救到，len=0 */
    scene_reset(&rd, SCENARIO_STANDARD, 7u);
    scene_add(&rd, 500.0, 0.0, 0.0, 5.0);        /* 生存 5t，但距离 500（50t）→ 必死 */
    memset(&plans[0], 0, sizeof(RoutePlan));
    plans[0].algo = ALGO_NN;
    plans[0].saved = 0;
    plans[0].dead = 1;
    plans[0].feasible = 0;
    plans[0].length = 0.0;
    plans[0].finish_time = 0.0;

    VEQ_INT("空方案导出返回 0",
           viz_write_trace(&rd, plans, 1, "out/tt_empty"), 0);
    json = read_all("out/tt_empty/trace.json");
    if (json != NULL) {
        VHAS("空方案 route 为 []", json, "\"route\": []");
        VHAS("空方案 legs 为 []", json, "\"legs\": []");
        VHAS("全牺牲目标标为 dead", json, "\"state\": \"dead\"");
        VCHECK(braces_balanced(json), "空方案 JSON 括号配平");
        VNOHAS("没有多余的逗号紧接右括号", json, ",]");
        VNOHAS("没有多余的逗号紧接右花括号", json, ",}");
        free(json);
    }

    /* (b) 零目标 */
    scene_reset(&rd, SCENARIO_STANDARD, 8u);
    memset(&plans[0], 0, sizeof(RoutePlan));
    plans[0].algo = ALGO_NN;
    VEQ_INT("零目标导出返回 0",
           viz_write_trace(&rd, plans, 1, "out/tt_zero"), 0);
    json = read_all("out/tt_zero/trace.json");
    if (json != NULL) {
        VHAS("零目标 targets 为 []", json, "\"targets\": []");
        VCHECK(braces_balanced(json), "零目标 JSON 括号配平");
        free(json);
    }

    /* (c) 两个方案，其中第二个 leg 更少：确认 plans 之间逗号正确 */
    scene_reset(&rd, SCENARIO_STANDARD, 9u);
    scene_add(&rd, 40.0, 0.0, 0.0, 100.0);
    scene_add(&rd, 0.0, 30.0, 0.0, 100.0);
    {
        int order[2] = { 0, 1 };
        mock_build_plan(&rd, order, 2, ALGO_NN, &plans[0]);
    }
    {
        int order[1] = { 1 };
        mock_build_plan(&rd, order, 1, ALGO_NN, &plans[1]);
    }
    VEQ_INT("两方案导出返回 0",
           viz_write_trace(&rd, plans, 2, "out/tt_two"), 0);
    json = read_all("out/tt_two/trace.json");
    if (json != NULL) {
        VCHECK(braces_balanced(json), "两方案 JSON 括号配平");
        VEQ_INT("两方案 legs 合计=3 段", count_sub(json, "\"from\": "), 3);
        VEQ_INT("两个 plan 对象", count_sub(json, "\"algo\": "), 2);
        free(json);
    }
}

/*============================================================================
 *  十一、8. 数值格式
 *===========================================================================*/
static void test_numeric_format(void)
{
    RescueData rd;
    RoutePlan  plan;
    char      *json;

    section("8. 数值格式（-0.0 与极小值）");

    scene_reset(&rd, SCENARIO_STANDARD, 11u);
    scene_add(&rd, -0.0, 1e-12, 0.0, 100.0);
    memset(&plan, 0, sizeof(plan));
    plan.algo = ALGO_NN;

    VEQ_INT("导出返回 0", viz_write_trace(&rd, &plan, 1, "out/tt_num"), 0);
    json = read_all("out/tt_num/trace.json");
    if (json != NULL) {
        VHAS("-0.0 与 1e-12 都输出 0.000", json, "\"p0\": [0.000, 0.000, 0.000]");
        VNOHAS("不出现 -0.000", json, "-0.000");
        free(json);
    }
}

/*============================================================================
 *  十二、9. 规模与耗时（仅信息，不作硬性判定）
 *===========================================================================*/
static void test_perf(void)
{
    RescueData rd;
    RoutePlan  plans[4];
    int        n_plans = 0;
    clock_t    t0, t1;
    double     ms;
    long       size = 0;
    int        rc;

    section("9. 12 目标 / 4 方案的导出规模（信息项）");

    scene_reset(&rd, SCENARIO_TIGHT, 4321u);
    scene_ring(&rd, 12, 160.0, 200.0, 10.0);
    {
        int i, order[12];
        for (i = 0; i < 12; ++i) order[i] = i;
        mock_build_plan(&rd, order, 12, ALGO_NN, &plans[0]);
        for (i = 0; i < 12; ++i) order[i] = 11 - i;
        mock_build_plan(&rd, order, 12, ALGO_URGENCY, &plans[1]);
        for (i = 0; i < 12; ++i) order[i] = (i * 5) % 12;
        mock_build_plan(&rd, order, 12, ALGO_2OPT, &plans[2]);
        mock_nearest_plan(&rd, &plans[3]);
        n_plans = 4;
    }

    t0 = clock();
    rc = viz_write_trace(&rd, plans, n_plans, "out/tt_perf");
    t1 = clock();
    ms = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;

    VEQ_INT("12 目标导出返回 0", rc, 0);
    VCHECK(ms < 300.0, "导出耗时 < 300 ms（纯文本写出，正常在 1 ms 量级）");
    vinfo("12 目标 × 4 方案导出耗时 %.2f ms", ms);

    {
        FILE *f = fopen("out/tt_perf/trace.json", "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fclose(f);
        }
    }
    vinfo("trace.json 大小约 %ld 字节", size);
    VCHECK(size > 0 && size < 200000, "文件体积在合理范围（<200 KB）");
}

/*============================================================================
 *  十三、沙箱：交互式调试
 *===========================================================================*/
static void print_menu(const RescueData *rd, int n_plans, int color)
{
    viz_printf("\n---------------- 视觉模块调试沙箱 ----------------\n");
    viz_printf("  场景：%s   目标数 %d   种子 %u   方案数 %d\n",
           scenario_name(rd->scenario), rd->target_count, rd->seed, n_plans);
    viz_printf("  ANSI 颜色：%s\n", color ? "开" : "关");
    viz_printf("  1) 重新打印仪表盘 + 俯视图 + 过程模拟\n");
    viz_printf("  2) 导出 trace 到 out/（然后用浏览器打开 preview.html）\n");
    viz_printf("  3) 把控制台报告另存为 out/report.txt\n");
    viz_printf("  4) 切换 ANSI 颜色\n");
    viz_printf("  5) 目标数 +1（输入 5- 为 -1），改动后自动重建场景\n");
    viz_printf("  6) 换一个随机种子重建场景\n");
    viz_printf("  7) 打印某个方案的名字（调试 plan_name）\n");
    viz_printf("  q) 退出\n");
    viz_printf("> ");
    fflush(stdout);
}

static void run_demo(void)
{
    RescueData rd;
    RoutePlan  plans[4];
    int        n_plans = 0;
    int        color = 1;
    int        count = 9;
    unsigned   seed  = 2026u;

    viz_printf("\n===== 视觉模块调试沙箱 =====\n");
    viz_printf("提示：这里只调用 viz_module.c 的接口，数据全部由 mock_data.c 手工构造。\n");

    for (;;) {
        char line[64];

        scene_reset(&rd, SCENARIO_TIGHT, seed);
        scene_ring(&rd, count, 140.0, 180.0, 8.0);
        {
            /* 三条不同顺序 + 最近邻，方便对比界面上的“算法对比”面板 */
            int order[MAX_TARGET_NUM];
            int i;
            for (i = 0; i < count; ++i) order[i] = i;
            mock_build_plan(&rd, order, count, ALGO_NN, &plans[0]);
            for (i = 0; i < count; ++i) order[i] = count - 1 - i;
            mock_build_plan(&rd, order, count, ALGO_URGENCY, &plans[1]);
            for (i = 0; i < count; ++i) order[i] = (i % 2 == 0) ? i : count - 1 - i;
            mock_build_plan(&rd, order, count, ALGO_2OPT, &plans[2]);
            mock_nearest_plan(&rd, &plans[3]);
            plans[3].algo = ALGO_BACKTRACK;
            n_plans = 4;
        }
        {
            /* 把推荐方案应用到 rd，供过程模拟使用 */
            int best = viz_choose_best(plans, n_plans);
            int i;
            if (best >= 0) {
                for (i = 0; i < plans[best].len; ++i) {
                    int idx = plans[best].seq[i];
                    rd.target_list[idx].is_saved  = 1;
                    rd.target_list[idx].save_time = plans[best].leg_t[i];
                    rd.best_seq[i] = idx;
                }
                for (i = plans[best].len; i < MAX_TARGET_NUM; ++i) rd.best_seq[i] = -1;
                rd.saved_count = plans[best].saved;
                rd.dead_count  = plans[best].dead;
            }
        }

        viz_set_color(color);
        viz_render_report(&rd, plans, n_plans, (const char *[]){
            "最近邻贪心 NN", "生存时间优先 EDF", "2-opt 局部搜索", "回溯搜索 (剪枝)" });
        viz_draw_ascii_map(&rd);
        draw_console_sim(&rd);

        print_menu(&rd, n_plans, color);
        if (fgets(line, sizeof(line), stdin) == NULL) break;

        if (line[0] == 'q' || line[0] == 'Q') break;

        switch (line[0]) {
            case '1':
                break;                       /* 直接进入下一轮，等于重印 */
            case '2':
                if (viz_write_trace(&rd, plans, n_plans, viz_default_out_dir()) == 0)
                    viz_printf("已生成 out/trace.js —— 浏览器打开 preview.html 即可看到本场景。\n");
                break;
            case '3': {
                FILE *f = fopen("out/report.txt", "wb");
                if (f == NULL) { viz_printf("无法写 out/report.txt\n"); break; }
                viz_set_stream(f);
                viz_render_report(&rd, plans, n_plans, (const char *[]){
                    "最近邻贪心 NN", "生存时间优先 EDF", "2-opt 局部搜索", "回溯搜索 (剪枝)" });
                viz_draw_ascii_map(&rd);
                viz_set_stream(NULL);
                fclose(f);
                viz_printf("已保存 out/report.txt（不含颜色码）\n");
                break;
            }
            case '4':
                color = !color;
                break;
            case '5': {
                char *p = line + 1;
                while (*p == ' ') ++p;
                if (*p == '-') { if (count > 1) count--; }
                else           { if (count < MAX_TARGET_NUM) count++; }
                break;
            }
            case '6':
                seed = (unsigned)(seed * 1103515245u + 12345u) % 100000u;
                if (seed == 0) seed = 1;
                break;
            case '7': {
                int a;
                char nm[64];
                for (a = 0; a < n_plans; ++a) {
                    plan_name(plans[a].algo, nm, (int)sizeof(nm));
                    viz_printf("  方案 %d：algo=%d → \"%s\"\n", a, plans[a].algo, nm);
                }
                viz_printf("> 回车继续"); fflush(stdout);
                if (fgets(line, sizeof(line), stdin) == NULL) return;
                break;
            }
            default:
                break;
        }
    }
    viz_set_color(1);
    viz_printf("\n沙箱已退出。\n");
}

/*============================================================================
 *  十四、生成预览用 trace
 *===========================================================================*/
static int emit_trace(const char *dir)
{
    RescueData rd;
    RoutePlan  plans[4];
    int        n_plans = 0;
    int        rc;

    scene_full(&rd, plans, &n_plans);
    rc = viz_write_trace(&rd, plans, n_plans, dir);
    if (rc == 0) {
        viz_printf("\n已生成 %s/trace.js 与 trace.json（9 目标 / 4 方案，模拟数据）\n", dir);
        viz_printf("用浏览器打开本目录下的 preview.html 即可看到这份数据的 3D 效果。\n");
        {
            int i;
            for (i = 0; i < n_plans; ++i) {
                char nm[64];
                plan_name(plans[i].algo, nm, (int)sizeof(nm));
                viz_printf("  %-22s 获救 %d/%d  航程 %8.2f  完成 %7.2f\n",
                       nm, plans[i].saved, rd.target_count,
                       plans[i].length, plans[i].finish_time);
            }
        }
    } else {
        viz_printf("导出失败（返回 %d）\n", rc);
    }
    return rc;
}

/*============================================================================
 *  十四·五、公开演示站：用测试桩数据批量生成多场景（V1 用测试台先上线）
 *----------------------------------------------------------------------------
 *  背景：队友 A/B/D 的模块还没接完，但站点想先上线给人玩。
 *  所以先用测试台的“手工场景 + 占位策略”生成 6 套数据，格式与正式版完全一致
 *  （catalog.js + <key>/trace.js），将来把 `viz_test --emit-all` 换成
 *  `space_rescue --trace-all` 即可无缝切换，前端一行都不用改。
 *  占位策略的名字统一带“（测试桩）”，界面上也会显示“测试数据”标记。
 *==========================================================================*/
typedef struct {
    const char *key;
    const char *title;
    const char *desc;
    int         n;
    double      dist;      /* 环形场景半径 */
    double      life;      /* 生存上限（逐个递减） */
    double      life_step;
    unsigned    seed;
} MockScene;

static const MockScene g_mock_scenes[] = {
    { "standard", "标准场景 · 9 目标",  "目标分散成一圈，生存时间宽裕，先看整体效果", 9,  140.0, 180.0, 12.0, 2026u },
    { "tight",    "紧急场景 · 12 目标", "生存时间紧：顺序选错会大量牺牲（切到反向/交替基线看红色）", 12, 110.0, 106.0, 4.0, 8u },
    { "cluster",  "星团场景 · 10 目标", "目标挤成一团且离基点很远，航程占比高",       10, 195.0, 170.0,  8.0, 23u   },
    { "wide",     "大尺度场景 · 8 目标", "半径很大、生存时间很宽，考察长航段调度",      8,  265.0, 300.0, 10.0, 31u   },
    { "random",   "随机场景 · 11 目标", "半径与生存时间都居中，结果随参数变化",       11, 175.0, 150.0,  9.0, 97u   },
    { "max",      "满规模 · 12 目标",   "目标数打满 MAX_TARGET_NUM，最拥挤的一局",     12, 150.0, 160.0,  7.0, 5u    }
};
#define MOCK_SCENE_COUNT ((int)(sizeof(g_mock_scenes) / sizeof(g_mock_scenes[0])))

/* 构造一套“4 条基线”方案，标签明确写成测试桩，避免冒充真实算法 */
static int mock_build_4plans(RescueData *rd, RoutePlan plans[4])
{
    int order[MAX_TARGET_NUM];
    int i, n = rd->target_count;

    mock_nearest_plan(rd, &plans[0]);
    snprintf(plans[0].label, sizeof(plans[0].label), "最近邻贪心（测试桩）");

    mock_urgency_plan(rd, &plans[1]);
    snprintf(plans[1].label, sizeof(plans[1].label), "生存优先贪心（测试桩）");

    for (i = 0; i < n; ++i) order[i] = n - 1 - i;          /* 反向：故意很差 */
    mock_build_plan(rd, order, n, ALGO_2OPT, &plans[2]);
    snprintf(plans[2].label, sizeof(plans[2].label), "反向顺序基线（测试桩）");

    for (i = 0; i < n; ++i) order[i] = (i % 2 == 0) ? i : n - 1 - i;
    mock_build_plan(rd, order, n, ALGO_BACKTRACK, &plans[3]);
    snprintf(plans[3].label, sizeof(plans[3].label), "交替顺序基线（测试桩）");

    return 4;
}

static int emit_all(const char *dir)
{
    FILE  *sink;
    int    i, ok = 0;
    RescueData rd;
    RoutePlan  plans[4];
    char   sub[400], catalog_path[600], absbuf[600], webdir[80];
    const char *slash;

    viz_set_data_source("mock");        /* 前端据此显示“测试数据”徽标 */

    /* catalog 里的路径要相对 web 根目录（即 index.html 所在目录），
     * 所以只取 dir 的最后一段，例如 "web/traces" -> "traces"。 */
    slash = strrchr(dir, '/');
    if (slash == NULL) slash = strrchr(dir, '\\');
    snprintf(webdir, sizeof(webdir), "%s", (slash != NULL) ? slash + 1 : dir);

    sink = tmpfile();
    if (sink != NULL) viz_set_stream(sink);   /* 逐场景细节不刷屏 */

    for (i = 0; i < MOCK_SCENE_COUNT; ++i) {
        const MockScene *sc = &g_mock_scenes[i];
        int n_plans;

        scene_reset(&rd, SCENARIO_TIGHT, sc->seed);
        scene_ring(&rd, sc->n, sc->dist, sc->life, sc->life_step);
        n_plans = mock_build_4plans(&rd, plans);

        snprintf(sub, sizeof(sub), "%s/%s", dir, sc->key);
        if (viz_write_trace(&rd, plans, n_plans, sub) == 0) ok++;
    }

    if (sink != NULL) { fclose(sink); viz_set_stream(NULL); }

    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.js", dir);
    {
        FILE *cat = fopen(catalog_path, "wb");
        if (cat == NULL) {
            viz_printf("无法写入 %s\n", catalog_path);
            return -1;
        }
        fprintf(cat, "/* 由 viz_test --emit-all 自动生成（测试桩数据，勿手工编辑）。\n"
                     " * 每场景一份 trace.js，页面按 ?trace=<key> 加载。 */\n");
        fprintf(cat, "window.RESCUE_TRACE_CATALOG = [\n");
        for (i = 0; i < MOCK_SCENE_COUNT; ++i) {
            const MockScene *sc = &g_mock_scenes[i];
            fprintf(cat,
                    "  { \"key\": \"%s\", \"title\": \"%s\", \"desc\": \"%s\",\n"
                    "    \"file\": \"%s/%s/trace.js\" }%s\n",
                    sc->key, sc->title, sc->desc, webdir, sc->key,
                    (i + 1 < MOCK_SCENE_COUNT) ? "," : "");
        }
        fprintf(cat, "];\n");
        fclose(cat);
    }

    viz_get_cwd(absbuf, (int)sizeof(absbuf));
    viz_printf("\n站点数据（测试桩）已生成：%d/%d 个场景\n", ok, MOCK_SCENE_COUNT);
    viz_printf("  目录：%s\\%s\n", absbuf, dir);
    viz_printf("  清单：%s\\%s\\catalog.js\n", absbuf, dir);
    viz_printf("  切到正式数据只需：space_rescue.exe --trace-all %s（前端不用改）\n\n", dir);
    return (ok == MOCK_SCENE_COUNT) ? 0 : -2;
}

/*============================================================================
 *  十五、入口
 *===========================================================================*/
static void print_usage(const char *prog)
{
    viz_printf("视觉模块测试程序（只依赖 viz_module.c）\n");
    viz_printf("用法：%s [选项]\n", prog);
    viz_printf("  （无参数）       跑全部测试并汇总 PASS/FAIL\n");
    viz_printf("  --demo           交互式调试沙箱（改场景 → 立刻看输出）\n");
    viz_printf("  --emit [dir]     生成单份预览用 trace（默认 out）\n");
    viz_printf("  --emit-all [dir] 用测试桩数据批量生成公开演示站的多场景（默认 web/traces）\n");
    viz_printf("  -h, --help       显示本帮助\n");
}

int main(int argc, char *argv[])
{
    int i;
    int quiet = 0;

    viz_console_init();      /* 控制台切到 UTF-8：否则中文/√× 全是乱码 */

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--demo") == 0) {
            (void)TEST_MKDIR("out");
            run_demo();
            return 0;
        }
        if (strcmp(argv[i], "--emit") == 0) {
            const char *dir = NULL;
            if (i + 1 < argc && argv[i + 1][0] != '-') dir = argv[++i];
            if (dir == NULL) dir = viz_default_out_dir();
            return (emit_trace(dir) == 0) ? 0 : 1;
        }
        if (strcmp(argv[i], "--emit-all") == 0) {
            const char *dir = "web/traces";
            if (i + 1 < argc && argv[i + 1][0] != '-') dir = argv[++i];
            return (emit_all(dir) == 0) ? 0 : 1;
        }
        if (strcmp(argv[i], "--quiet") == 0) quiet = 1;
    }

    (void)TEST_MKDIR("out");

    viz_printf("======================================================================\n");
    viz_printf("        视觉模块（队员 C）独立测试 —— 只加载 viz_module.c\n");
    viz_printf("======================================================================\n");
    viz_printf("说明：数据由 mock_data.c 手工构造，不依赖数据模块与算法模块。\n");
    viz_printf("      每个 √ 表示一条断言通过，× 会打印期望值与实际值。\n");
    {
        int cp = viz_output_codepage();
        if (cp == 65001) {
            viz_printf("环境：输出编码 = UTF-8（65001）  控制台代码页 = %d\n",
                       viz_console_codepage());
        } else {
            viz_printf("环境：输出编码 = %d（GBK/系统码页，适配 CLion 等 IDE 控制台）\n", cp);
            viz_printf("      如需改回 UTF-8：清空环境变量 RESCUE_OUT_CP 后重跑。\n");
        }
    }

    test_text_tools();
    test_choose_best();
    test_report();
    test_ascii_map();
    test_console_sim();
    test_trace_write();
    test_trace_edges();
    test_numeric_format();
    test_perf();

    viz_printf("\n======================================================================\n");
    viz_printf("  汇总：通过 %d 条，失败 %d 条，跳过 %d 条\n", g_pass, g_fail, g_skip);
    if (g_fail == 0) viz_printf("  结论：全部通过 √\n");
    else             viz_printf("  结论：存在 %d 条失败 ×（上方标 × 的即问题点）\n", g_fail);
    viz_printf("  产物：out/ 目录下的 trace.json / trace.js / report.txt\n");
    viz_printf("======================================================================\n");

    if (!quiet && g_fail == 0)
        viz_printf("\n提示：想手动看效果，运行  viz_test.exe --demo  ；\n"
               "      想在网页里看效果，运行  viz_test.exe --emit out  然后打开 preview.html。\n");

    return g_fail == 0 ? 0 : 1;
}
