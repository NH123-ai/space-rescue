/*============================================================================
 *  space_rescue.h  --  太空救援问题  共用头文件  (v2)
 *----------------------------------------------------------------------------
 *  说明：
 *    1) 本文件是四人分工的“契约”。结构体字段、已有函数名与参数表禁止个人
 *       私自改动；如需扩展，只能【在末尾追加】并在提交说明中登记。
 *    2) v2 相对 v1 的变更（仅追加，旧代码无需修改即可编译）：
 *         Target     追加 vx, vy, vz（漂移速度）、life_left（剩余生存时间）
 *         RescueData 追加 total_distance、saved_count、dead_count、algo_used
 *         新增 RoutePlan（单条救援方案的完整统计，供算法模块与可视化交换）
 *         新增组员 C 的可视化扩展接口 viz_write_trace / viz_render_report
 *    3) 依赖方向单向：
 *         数据模块(A)  <-  算法模块(B)  /  可视化模块(C)  <-  主程序(D)
 *===========================================================================*/
#ifndef SPACE_RESCUE_H
#define SPACE_RESCUE_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

/* ------------------------------ 宏定义 ------------------------------ */
#define MAX_TARGET_NUM     12      /* 最大待救援目标数量（回溯规模不宜过大） */
#define SPEED              10.0    /* 救援飞船速度（距离/时间单位）          */
#define UPDATE_INTERVAL    5.0     /* 坐标刷新间隔 t                         */

#define SCENARIO_STANDARD  0       /* 标准场景：目标分散、时间较宽裕         */
#define SCENARIO_TIGHT     1       /* 紧急场景：生存时间紧张                 */
#define SCENARIO_CLUSTER   2       /* 星团场景：目标聚集在远处               */
#define SCENARIO_WIDE      3       /* 大尺度场景：目标散布范围大             */
#define SCENARIO_RANDOM    4       /* 完全随机场景                           */

/* ------------------------------ 目标结构体 ------------------------------ */
typedef struct {
    double x, y, z;         /* 当前三维坐标                        */
    double life_max_time;   /* 最长生存时间，超过则救援失败        */
    int    is_saved;        /* 0 未救援，1 已救援                  */
    double save_time;       /* 实际被救援时刻（输出结果）          */

    /* ---- v2 追加字段 ---- */
    double vx, vy, vz;      /* 漂移速度向量（单位/时间，低速）     */
    double life_left;       /* 仿真用：当前剩余生存时间            */
} Target;

/* ------------------------------ 全局救援数据 ------------------------------ */
typedef struct {
    Target target_list[MAX_TARGET_NUM];
    int    target_count;              /* 实际目标总数                  */
    double total_time;                /* 总救援耗时                    */
    int    best_seq[MAX_TARGET_NUM];  /* 最优救援顺序，存目标下标      */
    int    find_success;              /* 1 找到可行方案，0 失败        */

    /* ---- v2 追加字段 ---- */
    double total_distance;            /* 最优方案总航程                */
    int    saved_count;               /* 成功救援数量                  */
    int    dead_count;                /* 死亡数量                      */
    int    algo_used;                 /* 当前采用方案编号（见 ALGO_*） */
    double origin_x, origin_y, origin_z; /* 救援队坐标（默认原点）     */
    unsigned seed;                    /* 场景随机种子（可复现）        */
    int    scenario;                  /* 场景类型 SCENARIO_*           */
} RescueData;

/* 算法编号，供 UI 与统计使用 */
#define ALGO_NN          0   /* 最近邻贪心                     */
#define ALGO_URGENCY     1   /* 生存时间优先（紧迫度贪心）     */
#define ALGO_BACKTRACK   2   /* 回溯搜索（精确/近似精确）      */
#define ALGO_2OPT        3   /* 2-opt 局部搜索                 */

/* ------------------------------ 单条救援方案 ------------------------------ */
typedef struct {
    int    seq[MAX_TARGET_NUM];  /* 救援顺序：目标下标             */
    int    len;                  /* 顺序长度                       */
    double leg_t[MAX_TARGET_NUM];/* 第 i 段航程结束时刻（相对 t=0）*/
    double leg_d[MAX_TARGET_NUM];/* 第 i 段航程距离                */
    double length;               /* 总航程                         */
    double finish_time;          /* 完成救援的总耗时               */
    int    saved;                /* 成功救援数                     */
    int    dead;                 /* 死亡数                         */
    int    feasible;             /* 1 = 全部获救（满足生存约束）   */
    long   nodes;                /* 搜索节点数（复杂度评价）       */
    double elapsed_ms;           /* 计算耗时（毫秒）               */
    int    algo;                 /* ALGO_*                         */
    /* ---- v2 追加：方案自定义显示名 ----
     * 为空时按 algo 取默认名；测试桩/实验性策略可写自己的名字，
     * 这样界面上的“算法对比”不会把占位实现冒充成真实算法。 */
    char   label[40];
} RoutePlan;

/*============================================================================
 *  组员 A：数据模块  data_module.c
 *===========================================================================*/
int    read_target_data(RescueData *rd, const char *filename);
void   print_target_info(RescueData *rd);
double calc_3d_distance(double x1, double y1, double z1,
                        double x2, double y2, double z2);
void   update_target_pos(Target *tg, double delta_t);

/* v2 追加：场景生成与重置（便于演示与自动化测试） */
void   gen_scenario(RescueData *rd, int count, int scenario, unsigned seed);
void   reset_target_states(RescueData *rd);
void   target_pos_at(const Target *tg, double t, double *px, double *py, double *pz);
/* v3 追加：队友 A 原始“随机抖动”漂移模型，保留作对照（主流程用匀速漂移） */
void   update_target_pos_random(Target *tg, double dt);

/*============================================================================
 *  组员 B：核心算法模块  algorithm_module.c
 *===========================================================================*/
void   tsp_greedy(RescueData *rd);                        /* 最近邻贪心 */
void   tsp_backtrack(RescueData *rd);                     /* 回溯搜索   */
int    check_time_constraint(RescueData *rd, int seq[], int len);

/* v2 追加：以 RoutePlan 为交换格式的方案族 */
void plan_nearest(RescueData *rd, RoutePlan *out);
void plan_urgency(RescueData *rd, RoutePlan *out);
void plan_backtrack(RescueData *rd, RoutePlan *out);
void plan_2opt(RescueData *rd, RoutePlan *out);
void plan_apply(RescueData *rd, const RoutePlan *plan);
void plan_name(int algo, char *buf, int bufsz);
/* 对给定顺序做一次完整仿真，返回统计（供算法模块与可视化共用） */
void plan_simulate(RescueData *rd, const int seq[], int len, RoutePlan *out);
/* v3 追加：队友 B 全排列回溯的搜索节点计数（用于算法效率对比） */
long tsp_last_nodes(void);

/*============================================================================
 *  组员 C：可视化演示模块  viz_module.c
 *===========================================================================*/
void draw_console_sim(RescueData *rd);            /* 控制台救援过程模拟 */

/* v2 追加：等级更高的可视化出口 -------------------------------- */
/* 1) 控制台仪表盘（ANSI 彩色，无需 EasyX，纯 C 标准库）         */
void viz_render_report(const RescueData *rd,
                       const RoutePlan *plans, int n_plans,
                       const char *plan_titles[]);
/* 2) 轨迹导出：写 trace.json 与 trace.js（供浏览器 WebGL/Canvas 渲染）*/
/*    out_dir 传 NULL 时写到 ./web ；返回 0 成功，负数失败        */
int  viz_write_trace(const RescueData *rd,
                     const RoutePlan *plans, int n_plans,
                     const char *out_dir);
/* 3) ASCII 俯视图（无 ANSI 的终端也能看）                        */
void viz_draw_ascii_map(const RescueData *rd);
/* 3.5) 路径工具：默认导出目录 / 当前工作目录（排查“trace 写到哪了”） */
const char *viz_default_out_dir(void);           /* 环境变量 RESCUE_OUT_DIR 或 "out" */
void viz_get_cwd(char *out, int outsz);          /* 当前工作目录（绝对路径）          */
/* 4) 小工具：场景名与“推荐方案”选择（获救数→航程→耗时）          */
const char *scenario_name(int sc);
int  viz_choose_best(const RoutePlan *plans, int n_plans);
void viz_set_color(int enable);   /* 0 = 关闭 ANSI 颜色（重定向到文件时）*/
/* 5) 终端对齐工具：按“显示列宽”（中文占 2 列）补齐，保证表格不错位 */
int  viz_width(const char *s);
void viz_pad_r(char *dst, int dstsz, const char *s, int width);
void viz_pad_l(char *dst, int dstsz, const char *s, int width);
/* 6) 输出流：默认 stdout；传 NULL 恢复 stdout。
 *    传文件指针可把仪表盘输出写进文件（测试断言 / 报告素材）。       */
void viz_set_stream(FILE *f);
/* 7) 控制台编码初始化：把控制台切到 UTF-8，避免中文输出乱码。
 *    主程序 / 测试程序请在 main() 第一行调用一次。                    */
void viz_console_init(void);
/* 8) 读取当前控制台输出代码页（Windows 返回 CP，其他平台/非控制台返回 0）。
 *    65001 = UTF-8；用来确认乱码是否已修好。                          */
int  viz_console_codepage(void);
/* 9) 输出编码（乱码开关）：
 *      viz_set_output_codepage(65001)  UTF-8（默认，重定向到文件/真实控制台）
 *      viz_set_output_codepage(936)    GBK（CLion 等按系统码页解码的控制台）
 *    也可用环境变量 RESCUE_OUT_CP 指定，viz_console_init() 会读取。   */
void viz_set_output_codepage(int cp);
int  viz_output_codepage(void);
/* 10) 统一的输出函数：printf 的直接替代（内部按上面的代码页转码）。
 *     正式程序 / 测试程序输出中文时请用这两个，不要用裸 printf。      */
void viz_printf(const char *fmt, ...);
void viz_vprintf(const char *fmt, va_list ap);
/* 11) 数据来源标记：写进 trace 的 meta.dataSource，前端会把“测试台占位数据”
 *     明确标出来，避免把测试桩当成真实算法结果。
 *       "full" = 正式模块生成（默认）；"mock" = 视觉模块测试台生成。 */
void viz_set_data_source(const char *src);

/* 可选：EasyX 原生动画（定义了 EASYX 宏时才编译，答辩可现场演示）*/
#ifdef EASYX
void init_easyx_window(void);
void draw_3d_projection(RescueData *rd, double now_time);
void run_rescue_animation(RescueData *rd);
#endif

/*============================================================================
 *  组员 D：主程序与测试模块  main.c
 *===========================================================================*/
void print_result(RescueData *rd);
void run_test_case(RescueData *rd, int case_id);
int  main(int argc, char *argv[]);

#endif /* SPACE_RESCUE_H */
