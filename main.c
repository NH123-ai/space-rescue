/*============================================================================
 *  main.c  --  组员 D：主程序与测试模块
 *----------------------------------------------------------------------------
 *  职责：命令行解析、场景调度、调用算法模块、输出结果、导出 trace、
 *        并内置一组可复现的测试用例（对应报告“软件测试”一章）。
 *
 *  用法示例：
 *    space_rescue                      默认演示：标准场景 9 个目标 + 导出 web trace
 *    space_rescue --n 12 --scenario tight --seed 2026
 *    space_rescue --scenario cluster --trace web
 *    space_rescue --test               依次运行内置测试用例
 *    space_rescue --menu               交互式菜单（可反复生成场景并演示）
 *    space_rescue --no-trace           只打印控制台报告，不生成 web 文件
 *    space_rescue --no-color           关闭 ANSI 颜色（重定向到文件时使用）
 *===========================================================================*/
#include "space_rescue.h"

/*---------------------------------------------------------------------------
 *  测试用例表：与报告中的“测试用例设计”一一对应
 *--------------------------------------------------------------------------*/
typedef struct {
    int    id;
    const char *desc;
    int    count;
    int    scenario;
    unsigned seed;
    int    expect_saved;      /* 期望值：最优方案能救回的目标数（基准值） */
} TestCase;

static const TestCase g_cases[] = {
    { 1, "单目标最小规模",              1,  SCENARIO_STANDARD, 101u, 1  },
    { 2, "标准 9 目标场景",             9,  SCENARIO_STANDARD, 7u,   9  },
    { 3, "紧急场景：生存时间紧张",       9,  SCENARIO_TIGHT,    11u,  9  },
    { 4, "星团场景：目标聚集",          10,  SCENARIO_CLUSTER,  23u,  10 },
    { 5, "大尺度场景：散布很远",         8,  SCENARIO_WIDE,     31u,  8  },
    { 6, "满规模 12 目标（回溯上限）",   12,  SCENARIO_STANDARD, 5u,   12 },
    { 7, "完全随机场景",                11,  SCENARIO_RANDOM,   97u,  11 },
    { 8, "极端紧迫：12 目标必有牺牲",    12,  SCENARIO_TIGHT,    8u,   9  }
};
#define CASE_COUNT ((int)(sizeof(g_cases) / sizeof(g_cases[0])))

/*---------------------------------------------------------------------------
 *  print_result：以 v1 接口约定的形式输出最终救援结果
 *--------------------------------------------------------------------------*/
void print_result(RescueData *rd)
{
    int i;
    char name[64];
    char h1[32], h2[32], h3[32];

    plan_name(rd->algo_used, name, (int)sizeof(name));

    viz_printf("\n================ 最终救援结果 ================\n");
    viz_printf("采用策略：%s\n", name);
    viz_printf("目标总数：%d   成功救援：%d   牺牲：%d   总耗时：%.2f   总航程：%.2f\n",
           rd->target_count, rd->saved_count, rd->dead_count,
           rd->total_time, rd->total_distance);
    viz_printf("---------------------------------------------\n");
    viz_pad_r(h1, sizeof(h1), "编号", 6);
    viz_pad_r(h2, sizeof(h2), "状态", 8);
    viz_pad_r(h3, sizeof(h3), "获救时刻", 10);
    viz_printf("  %s%s%s%s\n", h1, h2, h3, "备注");
    for (i = 0; i < rd->target_count; ++i) {
        const Target *tg = &rd->target_list[i];
        char id[16], st[16];

        snprintf(id, sizeof(id), "T%d", i + 1);
        viz_pad_r(st, sizeof(st), tg->is_saved ? "已获救" : "未获救", 8);
        if (tg->is_saved) {
            char idp[16];
            viz_pad_r(idp, sizeof(idp), id, 6);
            viz_printf("  %s%s%10.2f   剩余生存 %.2f\n",
                   idp, st, tg->save_time, tg->life_max_time - tg->save_time);
        } else {
            char idp[16];
            viz_pad_r(idp, sizeof(idp), id, 6);
            viz_printf("  %s%s%10s   生存上限 %.2f\n",
                   idp, st, "-", tg->life_max_time);
        }
    }
    viz_printf("=============================================\n\n");
}

/*---------------------------------------------------------------------------
 *  run_both_algorithms：运行全部四个策略，返回方案数量
 *--------------------------------------------------------------------------*/
static int run_all_plans(RescueData *rd, RoutePlan plans[], int max_plans)
{
    int n = 0;

    if (n < max_plans) { plan_nearest(rd,   &plans[n]); n++; }
    if (n < max_plans) { plan_urgency(rd,   &plans[n]); n++; }
    if (n < max_plans) { plan_2opt(rd,      &plans[n]); n++; }
    if (n < max_plans) { plan_backtrack(rd, &plans[n]); n++; }
    return n;
}

/*---------------------------------------------------------------------------
 *  run_test_case：执行一个内置测试用例，返回 1 通过 / 0 失败
 *--------------------------------------------------------------------------*/
void run_test_case(RescueData *rd, int case_id)
{
    RoutePlan plans[4];
    int  n_plans;
    int  i;

    if (case_id < 1 || case_id > CASE_COUNT) {
        viz_printf("[测试] 用例编号越界：%d（有效范围 1..%d）\n", case_id, CASE_COUNT);
        return;
    }
    {
        const TestCase *tc = &g_cases[case_id - 1];
        int best, pass;

        gen_scenario(rd, tc->count, tc->scenario, tc->seed);
        viz_printf("\n[测试用例 #%d] %s（n=%d, scenario=%d, seed=%u, 基准最佳获救数=%d）\n",
               tc->id, tc->desc, tc->count, tc->scenario, tc->seed, tc->expect_saved);
        n_plans = run_all_plans(rd, plans, 4);
        for (i = 0; i < n_plans; ++i) {
            char name[64];
            plan_name(plans[i].algo, name, (int)sizeof(name));
            viz_printf("   %-22s 获救 %2d/%2d  航程 %8.2f  节点 %8ld  耗时 %7.4f ms\n",
                   name, plans[i].saved, rd->target_count,
                   plans[i].length, plans[i].nodes, plans[i].elapsed_ms);
        }

        /* 判定：最优方案的获救数是否达到基准值 */
        best = viz_choose_best(plans, n_plans);
        pass = (best >= 0 && plans[best].saved == tc->expect_saved);
        viz_printf("   结论：%s（最优获救 %d/%d，基准 %d）\n",
               pass ? "PASS √" : "偏差（需人工复核）",
               (best >= 0) ? plans[best].saved : -1, rd->target_count, tc->expect_saved);
    }
}

/*---------------------------------------------------------------------------
 *  交互菜单：反复生成场景并演示（满足“一次运行可以多次演示”的交互要求）
 *--------------------------------------------------------------------------*/
static void interactive_menu(unsigned seed)
{
    RescueData rd;
    RoutePlan  plans[4];
    char        line[64];
    int         scenario = SCENARIO_STANDARD;
    int         count    = 9;
    int         round    = 1;

    for (;;) {
        int n_plans;

        gen_scenario(&rd, count, scenario, seed + (unsigned)round * 7919u);
        n_plans = run_all_plans(&rd, plans, 4);

        {
            const char *titles[4] = { "最近邻贪心 NN", "生存时间优先 EDF",
                                      "2-opt 局部搜索", "回溯搜索 (剪枝)" };
            viz_render_report(&rd, plans, n_plans, titles);
        }
        {
            int best = viz_choose_best(plans, n_plans);
            if (best >= 0) {
                plan_apply(&rd, &plans[best]);
                print_result(&rd);
                draw_console_sim(&rd);
            }
            viz_draw_ascii_map(&rd);
        }

        viz_printf("第 %d 轮演示完成。命令：[回车]=换一个随机场景  "
               "1..4=切换场景(0标准/1紧急/2星团/3大尺度/4随机)  "
               "+/-=增减目标数  q=退出\n> ", round);
        if (fgets(line, sizeof(line), stdin) == NULL) break;

        {
            char *p = line;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == 'q' || *p == 'Q') break;
            if (*p == '+') { if (count < MAX_TARGET_NUM) count++; round++; continue; }
            if (*p == '-') { if (count > 1) count--; round++; continue; }
            if (*p >= '0' && *p <= '4') { scenario = *p - '0'; round++; continue; }
            round++;
        }
    }
    viz_printf("演示结束。\n");
}

/*---------------------------------------------------------------------------
 *  命令行帮助
 *--------------------------------------------------------------------------*/
static void print_usage(const char *prog)
{
    viz_printf("太空救援问题 —— 综合程序设计五\n");
    viz_printf("用法：%s [选项]\n", prog);
    viz_printf("  --n <1..%d>        目标数量（默认 9）\n", MAX_TARGET_NUM);
    viz_printf("  --scenario <s>     场景：standard|tight|cluster|wide|random（默认 standard）\n");
    viz_printf("  --seed <u>         随机种子（默认 2026，可复现）\n");
    viz_printf("  --trace [dir]      导出单场景 trace 到 dir（默认 web）\n");
    viz_printf("  --trace-all [dir]  批量生成“公开演示站”的多场景数据（默认 web/traces）\n");
    viz_printf("  --no-trace         不导出 web trace\n");
    viz_printf("  --test             运行内置测试用例集\n");
    viz_printf("  --menu             交互式反复演示\n");
    viz_printf("  --no-color         关闭 ANSI 颜色\n");
    viz_printf("  -h, --help         显示本帮助\n");
}

static int parse_scenario(const char *s)
{
    if (s == NULL) return SCENARIO_STANDARD;
    if (strcmp(s, "tight")   == 0) return SCENARIO_TIGHT;
    if (strcmp(s, "cluster") == 0) return SCENARIO_CLUSTER;
    if (strcmp(s, "wide")    == 0) return SCENARIO_WIDE;
    if (strcmp(s, "random")  == 0) return SCENARIO_RANDOM;
    return SCENARIO_STANDARD;
}

/*===========================================================================
 *  --trace-all：为“公开站点”批量生成多套场景数据
 *----------------------------------------------------------------------------
 *  公开演示站不能现场跑 C 程序（静态托管没有后端），所以把几套典型场景
 *  预先算好，各自写一套 trace，再写一份 catalog.js 供前端做“场景切换”。
 *  目录结构：
 *      web/traces/catalog.js          ← 场景清单
 *      web/traces/<key>/trace.js      ← 各场景数据（页面按 key 加载）
 *      web/traces/<key>/trace.json
 *  这样部署到 GitHub Pages / Vercel / Netlify 后，任何人打开网址就能玩。
 *==========================================================================*/
typedef struct {
    const char *key;      /* URL 参数值，如 ?trace=tight          */
    const char *title;    /* 下拉框里的名字                        */
    const char *desc;     /* 一句话说明                            */
    int         n;        /* 目标数量                              */
    int         scenario; /* SCENARIO_*                            */
    unsigned    seed;     /* 随机种子（可复现）                    */
} SiteScene;

static const SiteScene g_site_scenes[] = {
    { "standard", "标准场景 · 9 目标",  "目标分散、生存时间宽裕，先看整体效果", 9,  SCENARIO_STANDARD, 2026u },
    { "tight",    "紧急场景 · 12 目标", "生存时间紧张，必然有目标牺牲（红色）", 12, SCENARIO_TIGHT,    8u    },
    { "cluster",  "星团场景 · 10 目标", "目标聚成一团、离基点很远",            10, SCENARIO_CLUSTER,  23u   },
    { "wide",     "大尺度场景 · 8 目标", "散布范围极大，航程占比高",             8,  SCENARIO_WIDE,     31u   },
    { "random",   "随机场景 · 11 目标", "完全随机，可反复挑战",                 11, SCENARIO_RANDOM,   97u   },
    { "max",      "满规模 · 12 目标",   "回溯搜索的规模上限",                   12, SCENARIO_STANDARD, 5u    }
};
#define SITE_SCENE_COUNT ((int)(sizeof(g_site_scenes) / sizeof(g_site_scenes[0])))

static int write_site_traces(const char *dir)
{
    int         i, n_plans, ok = 0;
    RescueData  rd;
    RoutePlan   plans[4];
    char        sub[400], catalog_path[512], webdir[64];
    FILE       *cat;
    const char *p;
    FILE       *sink;         /* 临时把逐场景的详细输出丢掉，最后只打一行汇总 */

    /* 取目录最后一段作为“相对 web 根目录”的文件夹名，用于 catalog 里的相对路径 */
    p = strrchr(dir, '/');
    if (p == NULL) p = strrchr(dir, '\\');
    snprintf(webdir, sizeof(webdir), "%s", (p != NULL) ? p + 1 : dir);

    sink = tmpfile();
    if (sink != NULL) viz_set_stream(sink);

    for (i = 0; i < SITE_SCENE_COUNT; ++i) {
        const SiteScene *sc = &g_site_scenes[i];
        gen_scenario(&rd, sc->n, sc->scenario, sc->seed);
        n_plans = run_all_plans(&rd, plans, 4);
        snprintf(sub, sizeof(sub), "%s/%s", dir, sc->key);
        if (viz_write_trace(&rd, plans, n_plans, sub) == 0) ok++;
    }

    if (sink != NULL) {
        fclose(sink);
        viz_set_stream(NULL);
    }

    /* 写场景清单 catalog.js */
    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.js", dir);
    cat = fopen(catalog_path, "wb");
    if (cat == NULL) {
        viz_printf("[主程序] 无法写入 %s\n", catalog_path);
        return -1;
    }
    fprintf(cat, "/* 由 space_rescue --trace-all 自动生成，请勿手工编辑。\n"
                 " * 每个场景一份 trace.js，页面按 ?trace=<key> 加载。 */\n");
    fprintf(cat, "window.RESCUE_TRACE_CATALOG = [\n");
    for (i = 0; i < SITE_SCENE_COUNT; ++i) {
        const SiteScene *sc = &g_site_scenes[i];
        fprintf(cat,
                "  { \"key\": \"%s\", \"title\": \"%s\", \"desc\": \"%s\",\n"
                "    \"file\": \"%s/%s/trace.js\" }%s\n",
                sc->key, sc->title, sc->desc, webdir, sc->key,
                (i + 1 < SITE_SCENE_COUNT) ? "," : "");
    }
    fprintf(cat, "];\n");
    fclose(cat);

    {
        char absbuf[600];
        viz_get_cwd(absbuf, (int)sizeof(absbuf));
        viz_printf("\n[主程序] 站点数据已生成：%d/%d 个场景\n", ok, SITE_SCENE_COUNT);
        viz_printf("         目录   : %s\\%s\n", absbuf, dir);
        viz_printf("         清单   : %s\\%s/catalog.js\n", absbuf, dir);
        viz_printf("         预览   : 打开 web/index.html（或部署后访问网址，用 ?trace=tight 切换场景）\n\n",
                   webdir);
    }
    return (ok == SITE_SCENE_COUNT) ? 0 : -2;
}

/*---------------------------------------------------------------------------
 *  main
 *--------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    RescueData rd;
    RoutePlan  plans[4];
    const char *titles[4] = { "最近邻贪心 NN", "生存时间优先 EDF",
                              "2-opt 局部搜索", "回溯搜索 (剪枝)" };
    int    count       = 9;
    int    scenario    = SCENARIO_STANDARD;
    unsigned seed      = 2026u;
    int    do_trace    = 1;
    const char *trace_dir = NULL;   /* NULL = 未指定，运行时再决定（见下方导出段） */
    int    do_test     = 0;
    int    do_menu     = 0;
    const char *all_dir = NULL;     /* --trace-all 的输出目录 */
    int    i, n_plans;

    viz_console_init();      /* 控制台切到 UTF-8：否则中文全是乱码 */

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
            if (count < 1) count = 1;
            if (count > MAX_TARGET_NUM) count = MAX_TARGET_NUM;
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = parse_scenario(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--trace") == 0) {
            do_trace = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') trace_dir = argv[++i];
        } else if (strcmp(argv[i], "--trace-all") == 0) {
            all_dir = "web/traces";
            if (i + 1 < argc && argv[i + 1][0] != '-') all_dir = argv[++i];
        } else if (strcmp(argv[i], "--no-trace") == 0) {
            do_trace = 0;
        } else if (strcmp(argv[i], "--test") == 0) {
            do_test = 1;
        } else if (strcmp(argv[i], "--menu") == 0) {
            do_menu = 1;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            viz_set_color(0);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    viz_printf("\n");
    viz_printf("################################################################\n");
    viz_printf("#            太空救援问题 · 综合程序设计五                     #\n");
    viz_printf("#   数据模块(A) / 算法模块(B) / 可视化模块(C) / 主程序(D)      #\n");
    viz_printf("################################################################\n");

    /* ---------- 测试模式 ---------- */
    if (do_test) {
        for (i = 1; i <= CASE_COUNT; ++i) run_test_case(&rd, i);
        viz_printf("\n[测试] 全部 %d 个用例执行完毕。\n\n", CASE_COUNT);
        return 0;
    }

    /* ---------- 交互模式 ---------- */
    if (do_menu) {
        interactive_menu(seed);
        return 0;
    }

    /* ---------- 公开演示站：批量生成多场景数据 ---------- */
    if (all_dir != NULL) {
        return (write_site_traces(all_dir) == 0) ? 0 : 1;
    }

    /* ---------- 单次演示模式 ---------- */
    gen_scenario(&rd, count, scenario, seed);
    print_target_info(&rd);

    n_plans = run_all_plans(&rd, plans, 4);
    viz_render_report(&rd, plans, n_plans, titles);

    {
        int best = viz_choose_best(plans, n_plans);
        if (best >= 0) {
            plan_apply(&rd, &plans[best]);
            print_result(&rd);
            draw_console_sim(&rd);
            viz_draw_ascii_map(&rd);
        }
    }

    if (do_trace) {
        /* 目录优先级：命令行 --trace <dir> > 环境变量 RESCUE_OUT_DIR > 默认 "web"。
         * CLion 的运行配置用 $PROJECT_DIR$ 注入绝对路径，
         * 避免“工作目录是构建目录”时 trace 被写进 cmake-build-debug\web\。 */
        const char *dir = trace_dir;
        if (dir == NULL || *dir == '\0') dir = getenv("RESCUE_OUT_DIR");
        if (dir == NULL || *dir == '\0') dir = "web";
        if (viz_write_trace(&rd, plans, n_plans, dir) != 0)
            viz_printf("[主程序] trace 导出失败（可加 --no-trace 跳过）\n");
    }

    return 0;
}
